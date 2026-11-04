"""Tk application framework for the trigger debug demo."""

from .dialogs import DialogPort, FakeDialogService, TkDialogService
from .errors import ErrorBoundary, install_tk_error_handler
from .page import LegacyPageAdapter, NavigationDecision, Page, PageHost, PageSpec
from .pump import PumpSink, UiEvent, UiPump
from .router import PageRouter
from .scheduler import FakeScheduler, SchedulerPort, TkScheduler
from .settings import DEFAULT_SETTINGS, load_settings, save_settings
from .state import (
    AckSlice,
    ConnectionSlice,
    ConsoleStore,
    ConsoleUiVector,
    MaskSlice,
    RunSlice,
    UiSlice,
)
from .theme import DEFAULT_THEME, ThemeTokens, apply_tk_scaling
from .widgets import ErrorBanner, LogView
from .workflow import (
    StepSpec,
    WorkflowContext,
    WorkflowNotResumable,
    WorkflowRunner,
    WorkflowSpec,
    WorkflowState,
)

__all__ = [
    "AckSlice",
    "ConnectionSlice",
    "ConsoleStore",
    "ConsoleUiVector",
    "DEFAULT_SETTINGS",
    "DEFAULT_THEME",
    "DialogPort",
    "ErrorBanner",
    "ErrorBoundary",
    "FakeDialogService",
    "FakeScheduler",
    "apply_tk_scaling",
    "install_tk_error_handler",
    "LegacyPageAdapter",
    "load_settings",
    "LogView",
    "MaskSlice",
    "NavigationDecision",
    "Page",
    "PageHost",
    "PageRouter",
    "PageSpec",
    "PumpSink",
    "RunSlice",
    "save_settings",
    "SchedulerPort",
    "StepSpec",
    "ThemeTokens",
    "TkDialogService",
    "TkScheduler",
    "UiEvent",
    "UiPump",
    "UiSlice",
    "WorkflowContext",
    "WorkflowNotResumable",
    "WorkflowRunner",
    "WorkflowSpec",
    "WorkflowState",
]
