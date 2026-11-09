"""Durable, step-based workflow engine for long-running console flows."""

from __future__ import annotations

import json
import threading
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable


class WorkflowNotResumable(RuntimeError):
    pass


@dataclass
class WorkflowState:
    workflow_id: str
    version: str
    run_id: str
    cursor: int = 0
    status: str = "CREATED"
    inputs: dict = field(default_factory=dict)
    step_results: dict = field(default_factory=dict)
    step_data: dict = field(default_factory=dict)
    active_step: str | None = None
    error: str = ""
    updated_at: str = ""

    def to_dict(self) -> dict:
        return {
            "workflow_id": self.workflow_id,
            "version": self.version,
            "run_id": self.run_id,
            "cursor": self.cursor,
            "status": self.status,
            "inputs": self.inputs,
            "step_results": self.step_results,
            "step_data": self.step_data,
            "active_step": self.active_step,
            "error": self.error,
            "updated_at": self.updated_at,
        }

    @classmethod
    def from_dict(cls, raw: dict) -> "WorkflowState":
        return cls(
            workflow_id=str(raw["workflow_id"]),
            version=str(raw["version"]),
            run_id=str(raw["run_id"]),
            cursor=int(raw.get("cursor", 0)),
            status=str(raw.get("status", "CREATED")),
            inputs=dict(raw.get("inputs", {})),
            step_results=dict(raw.get("step_results", {})),
            step_data=dict(raw.get("step_data", {})),
            active_step=raw.get("active_step"),
            error=str(raw.get("error", "")),
            updated_at=str(raw.get("updated_at", "")),
        )


@dataclass(frozen=True)
class StepSpec:
    id: str
    title: str
    run: Callable[["WorkflowContext"], None]
    resume_safe: bool = True


@dataclass(frozen=True)
class WorkflowSpec:
    id: str
    version: str
    steps: tuple[StepSpec, ...]

    def __post_init__(self) -> None:
        ids = [step.id for step in self.steps]
        if not ids:
            raise ValueError("workflow needs at least one step")
        if len(ids) != len(set(ids)):
            raise ValueError("workflow step ids must be unique")


class WorkflowContext:
    def __init__(self, spec: WorkflowSpec, state: WorkflowState,
                 path: Path, cancel_event: threading.Event) -> None:
        self.spec = spec
        self.state = state
        self.path = path
        self.cancel_event = cancel_event

    @property
    def step_data(self) -> dict:
        return self.state.step_data

    def checkpoint(self, **values) -> None:
        self.state.step_data.update(values)
        self.save()

    def cancel_requested(self) -> bool:
        return self.cancel_event.is_set()

    def save(self) -> None:
        self.state.updated_at = time.strftime("%Y-%m-%dT%H:%M:%S")
        _atomic_write(self.path, self.state.to_dict())


class WorkflowRunner:
    """Run one workflow; one runner owns one state file at a time."""

    def __init__(self, spec: WorkflowSpec, state_dir: Path | None = None) -> None:
        self.spec = spec
        self.state_dir = state_dir or Path("out/workflows")
        self.cancel_event = threading.Event()
        self.last_run_id: str | None = None

    def start(self, inputs: dict, run_id: str | None = None) -> WorkflowState:
        run_id = run_id or f"{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex[:6]}"
        self.last_run_id = run_id
        state = WorkflowState(self.spec.id, self.spec.version, run_id,
                              inputs=dict(inputs))
        context = self._context(state)
        context.save()
        self._execute(context)
        return state

    def resume(self, run_id: str) -> WorkflowState:
        self.last_run_id = run_id
        state = self.load(run_id)
        if state.status == "SUCCEEDED":
            raise WorkflowNotResumable("workflow already succeeded")
        step = self._current_step(state)
        if step is not None and state.active_step == step.id and not step.resume_safe:
            raise WorkflowNotResumable(
                f"step {step.id} is not resume-safe; restart the workflow")
        state.status = "RUNNING"
        state.error = ""
        state.active_step = None
        context = self._context(state)
        context.save()
        self._execute(context)
        return state

    def load(self, run_id: str) -> WorkflowState:
        path = self.state_path(run_id)
        raw = json.loads(path.read_text(encoding="utf-8"))
        state = WorkflowState.from_dict(raw)
        if state.workflow_id != self.spec.id:
            raise ValueError(f"run {run_id} belongs to {state.workflow_id}")
        if state.version != self.spec.version:
            raise ValueError(
                f"workflow version mismatch: {state.version} != {self.spec.version}")
        return state

    def latest(self) -> WorkflowState | None:
        candidates = sorted(self.state_dir.glob(f"{self.spec.id}-*.json"))
        if not candidates:
            return None
        return self.load(candidates[-1].stem[len(self.spec.id) + 1:])

    def cancel(self) -> None:
        self.cancel_event.set()

    def state_path(self, run_id: str) -> Path:
        return self.state_dir / f"{self.spec.id}-{run_id}.json"

    def _context(self, state: WorkflowState) -> WorkflowContext:
        return WorkflowContext(self.spec, state, self.state_path(state.run_id),
                               self.cancel_event)

    def _current_step(self, state: WorkflowState) -> StepSpec | None:
        if state.cursor < 0 or state.cursor >= len(self.spec.steps):
            return None
        return self.spec.steps[state.cursor]

    def _execute(self, context: WorkflowContext) -> None:
        state = context.state
        state.status = "RUNNING"
        while state.cursor < len(self.spec.steps):
            step = self.spec.steps[state.cursor]
            if self.cancel_event.is_set():
                state.status = "CANCELLED"
                state.active_step = None
                context.save()
                return
            state.active_step = step.id
            context.save()
            try:
                step.run(context)
            except Exception as exc:
                state.status = "CANCELLED" if self.cancel_event.is_set() else "FAILED"
                state.error = str(exc)
                context.save()
                raise
            state.step_results[step.id] = {
                "ok": True,
                "finished_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
            }
            state.cursor += 1
            state.active_step = None
            context.save()
        state.status = "SUCCEEDED"
        state.active_step = None
        context.save()


def _atomic_write(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2),
                   encoding="utf-8")
    for attempt in range(5):
        try:
            tmp.replace(path)
            return
        except PermissionError:
            if attempt == 4:
                raise
            time.sleep(0.02)
