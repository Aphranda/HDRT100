"""Conventional single-board OTA expressed as a durable workflow."""

from __future__ import annotations

import time

try:
    from . import ota
    from .ui.workflow import StepSpec, WorkflowContext, WorkflowRunner, WorkflowSpec
except ImportError:  # direct script execution
    import ota  # type: ignore
    from ui.workflow import (  # type: ignore
        StepSpec,
        WorkflowContext,
        WorkflowRunner,
        WorkflowSpec,
    )


class OtaWorkflow:
    """Adapter from OtaPage operations to WorkflowSpec steps."""

    def __init__(self, page, state_dir=None) -> None:
        self.page = page
        self.spec = WorkflowSpec(
            "ota.conventional",
            "1",
            (
                StepSpec("snapshot", "Read before-state", self._snapshot),
                StepSpec("begin", "OTA BEGIN/PBEGIN", self._begin, resume_safe=False),
                StepSpec("stream", "Stream image blocks", self._stream),
                StepSpec("end", "OTA END", self._end, resume_safe=False),
                StepSpec("wait", "Wait for ready", self._wait),
                StepSpec("receipt", "Write receipt", self._receipt, resume_safe=False),
            ),
        )
        self.runner = WorkflowRunner(self.spec, state_dir)

    def start(self, image: "ota.OtaImage", block_size: int, package: bool,
              auto_receipt: bool, backend: str, resource: str):
        inputs = {
            "image_path": str(image.path),
            "block_size": int(block_size),
            "package": bool(package),
            "auto_receipt": bool(auto_receipt),
            "backend": backend,
            "resource": resource,
        }
        try:
            return self.runner.start(inputs)
        except Exception as exc:
            self._failure_receipt(image, exc)
            raise

    def resume(self, run_id: str):
        state = self.runner.load(run_id)
        image = ota.OtaImage.load(state.inputs["image_path"])
        try:
            return self.runner.resume(run_id)
        except Exception as exc:
            self._failure_receipt(image, exc)
            raise

    def cancel(self) -> None:
        self.runner.cancel()

    def latest(self):
        return self.runner.latest()

    # ------------------------------------------------------------------ steps
    def _image(self, ctx: WorkflowContext) -> "ota.OtaImage":
        return ota.OtaImage.load(ctx.state.inputs["image_path"])

    def _snapshot(self, ctx: WorkflowContext) -> None:
        before = self.page._snapshot()
        self.page._emit_state(before)
        ctx.checkpoint(before=before)

    def _begin(self, ctx: WorkflowContext) -> None:
        image = self._image(ctx)
        begin = "SYSTem:OTA:PBEGIN" if ctx.state.inputs["package"] else "SYSTem:OTA:BEGin"
        response = self.page._request(f"{begin} {image.size},{image.crc32}", 20.0)
        self.page._record(f"{begin} → {response}")
        if response.strip() in ("0", "<timeout>"):
            raise RuntimeError(f"BEGIN 被拒绝：{response}")
        ctx.checkpoint(begin=begin)

    def _stream(self, ctx: WorkflowContext) -> None:
        image = self._image(ctx)
        block_size = int(ctx.state.inputs["block_size"])
        chunks = list(ota.chunks(image, block_size))
        start_index = int(ctx.step_data.get("chunk_index", 0))
        total = 0
        if start_index:
            expected = sum(len(chunk) for _offset, chunk in chunks[:start_index])
            try:
                progress = ota.parse_progress(
                    self.page._request("SYSTem:OTA:PROGress?", 5.0))
                received = int(progress["received"])
            except Exception as exc:
                raise RuntimeError(f"恢复前无法确认 OTA 进度：{exc}") from exc
            if received != expected:
                raise RuntimeError(
                    f"OTA 恢复偏移不一致：device={received} expected={expected}")
            total = expected
        last_received = -1
        stalls = 0
        for index, (_offset, chunk) in enumerate(chunks):
            if index < start_index:
                continue
            if self.page._cancel_event.is_set() or ctx.cancel_requested():
                try:
                    self.page._request("SYSTem:OTA:ABORt", 10.0)
                finally:
                    raise RuntimeError("用户中止")
            payload = b"SYSTem:OTA:DATA " + ota.scpi_block(chunk)
            result = self.page.app.dispatcher.request_bytes(
                payload, "ota", 30.0, expect_response=False)
            if result.error:
                raise RuntimeError(result.error)
            total += len(chunk)
            # Legacy DATA returns no query payload; pace and use PROGRESS as
            # the back-pressure signal.
            time.sleep(0.008)
            if index % 16 == 0 or total >= image.size:
                self.page._progress(total, image.size)
                progress = ota.parse_progress(
                    self.page._request("SYSTem:OTA:PROGress?", 5.0))
                received = int(progress["received"])
                self.page._progress(received, image.size)
                if received <= last_received:
                    stalls += 1
                    if stalls >= 3:
                        raise RuntimeError(
                            f"OTA 进度停滞在 {received}/{image.size}")
                else:
                    stalls = 0
                last_received = received
            ctx.checkpoint(chunk_index=index + 1, total=total)

    def _end(self, ctx: WorkflowContext) -> None:
        response = self.page._request("SYSTem:OTA:END", 30.0)
        self.page._record(f"END → {response}")
        if response.strip() in ("0", "<timeout>"):
            raise RuntimeError(f"END 被拒绝：{response}")
        ctx.checkpoint(end=response)

    def _wait(self, ctx: WorkflowContext) -> None:
        stage, status = self.page._wait_ready()
        ctx.checkpoint(stage=stage, status=status)

    def _receipt(self, ctx: WorkflowContext) -> None:
        image = self._image(ctx)
        before = ctx.step_data.get("before", {})
        after = self.page._snapshot()
        self.page._emit_state(after)
        stage = str(ctx.step_data.get("stage", "UNKNOWN"))
        status = ctx.step_data.get("status", {})
        passed = stage in ("READY_TO_REBOOT", "APPLIED") and \
            status.get("last_result", 1) == 0
        transfer = {
            "block_size": int(ctx.state.inputs["block_size"]),
            "chunks": (image.size + int(ctx.state.inputs["block_size"]) - 1)
            // int(ctx.state.inputs["block_size"]),
            "begin": ctx.step_data.get("begin", ""),
            "end": "SYSTem:OTA:END",
        }
        self.page._finish(
            image, before, after, stage, passed, transfer,
            bool(ctx.state.inputs["auto_receipt"]),
            str(ctx.state.inputs["backend"]), str(ctx.state.inputs["resource"]),
        )
        ctx.checkpoint(receipt_done=True)
        if not passed:
            raise RuntimeError(f"OTA 未进入可启动状态：{stage}")

    def _failure_receipt(self, image, exc: Exception) -> None:
        run_id = self.runner.last_run_id
        if not run_id:
            return
        try:
            state = self.runner.load(run_id)
        except Exception:
            return
        if state.step_data.get("receipt_done"):
            return
        before = state.step_data.get("before", {})
        if not before:
            return
        try:
            after = self.page._snapshot()
        except Exception:
            after = {}
        try:
            self.page._finish(
                image, before, after, "FAILED", False,
                {"error": str(exc)},
                bool(state.inputs.get("auto_receipt", False)),
                str(state.inputs.get("backend", "")),
                str(state.inputs.get("resource", "")),
            )
        except Exception:
            pass
