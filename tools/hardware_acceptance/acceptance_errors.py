"""Shared acceptance exceptions.

Kept in its own module so the workflow and its dependency modules can raise the
same exception type without importing the monolithic step library.
"""
from __future__ import annotations


class AcceptanceError(RuntimeError):
    """A mandatory acceptance condition was not met."""
