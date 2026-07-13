#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
"""Merge HIXL-related plog events chronologically across one or more endpoints."""

import argparse
import logging
import os
import re
import sys
from collections import defaultdict, namedtuple
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

logger = logging.getLogger(__name__)

ASCEND_TIMESTAMP = re.compile(
    r"(?P<date>20\d{2}-\d{2}-\d{2})[- ](?P<time>\d{2}:\d{2}:\d{2})[.]"
    r"(?P<millis>\d{3})[.](?P<micros>\d{3})"
)
COMPACT_TIMESTAMP = re.compile(
    r"\b[EIWF](?P<date>20\d{6})\s+(?P<time>\d{2}:\d{2}:\d{2})[.]"
    r"(?P<micros>\d{6})"
)
FALLBACK_TIMESTAMP = re.compile(
    r"\b(?P<date>20\d{2}-\d{2}-\d{2})[ T](?P<time>\d{2}:\d{2}:\d{2})[.]"
    r"(?P<fraction>\d{3,6})"
)
ERROR_LEVEL_PATTERN = re.compile(r"\[(?:ERROR|ERR0R)\]|\bE20\d{6}\b", re.IGNORECASE)
COMM_PATTERN = re.compile(r"(?:commIdentifier|comm)\[([^\]]+)\]", re.IGNORECASE)

MODULE_MARKERS = (
    ("HIXL_CS", ("HixlCS", "HixlClient", "HixlServer", "HIXL CS")),
    ("FabricMem", ("FabricMem", "Fabric mem")),
    ("ADXL", ("ADXL", "Adxl", "AscendDirect")),
    ("HIXL", ("[HIXL]",)),
    ("HCCL", ("HCCL", "Hccl")),
    ("HCCP", ("HCCP",)),
    ("RUNTIME", ("RUNTIME", "RtStream", "aclrt")),
    ("DRV", ("DRV", "devmm", "devdrv", "halMem", "halShmem")),
)
DISCRIMINATIVE_MARKERS = (
    "HcclCommPrepare",
    "LINK_ERROR_INFO",
    "CheckMemCpyAttr",
    "remoteBuffer",
    "Connect statistic",
    "Direct transfer statistic",
    "Fabric mem transfer statistic",
    "halMemImport",
    "suspect remote",
)


class ScanStats:
    """Collect scan facts without requiring the dataclasses module."""

    def __init__(self) -> None:
        self.files = 0
        self.timestamped_events = 0
        self.unparsed_relevant_errors = 0
        self.unreadable_files = 0


Source = namedtuple("Source", ("label", "directory"))
Event = namedtuple("Event", ("timestamp", "source", "path", "line_number", "level", "module", "line"))


def parse_datetime(value: str, time_format: str) -> Optional[datetime]:
    """Return None for malformed timestamps instead of aborting the full scan."""
    try:
        return datetime.strptime(value, time_format)
    except ValueError:
        return None


def parse_timestamp(line: str) -> Optional[datetime]:
    """Parse known Ascend plog and application timestamp formats."""
    match = ASCEND_TIMESTAMP.search(line)
    if match:
        value = f"{match['date']} {match['time']}.{match['millis']}{match['micros']}"
        return parse_datetime(value, "%Y-%m-%d %H:%M:%S.%f")

    match = COMPACT_TIMESTAMP.search(line)
    if match:
        value = f"{match['date']} {match['time']}.{match['micros']}"
        return parse_datetime(value, "%Y%m%d %H:%M:%S.%f")

    match = FALLBACK_TIMESTAMP.search(line)
    if match:
        fraction = match["fraction"].ljust(6, "0")
        value = f"{match['date']} {match['time']}.{fraction}"
        return parse_datetime(value, "%Y-%m-%d %H:%M:%S.%f")
    return None


def classify_module(line: str) -> str:
    """Return the closest HIXL stack component marker present in a line."""
    for module, markers in MODULE_MARKERS:
        if any(marker.lower() in line.lower() for marker in markers):
            return module
    return "UNKNOWN"


def classify_level(line: str) -> str:
    """Classify the log severity needed for chronological triage."""
    if ERROR_LEVEL_PATTERN.search(line):
        return "ERROR"
    if re.search(r"\bWARN(?:ING)?\b|\bW20\d{6}\b", line, re.IGNORECASE):
        return "WARN"
    if re.search(r"\bINFO\b|\bI20\d{6}\b", line, re.IGNORECASE):
        return "INFO"
    return "OTHER"


def is_discriminative(line: str) -> bool:
    """Select INFO events that distinguish common HIXL failure hypotheses."""
    return any(marker.lower() in line.lower() for marker in DISCRIMINATIVE_MARKERS)


def is_relevant(line: str) -> bool:
    """Limit causal candidates to the HIXL stack instead of arbitrary app errors."""
    return classify_module(line) != "UNKNOWN" or is_discriminative(line)


def iter_log_files(directory: Path) -> Iterable[Path]:
    """Yield readable regular files in a deterministic order."""
    try:
        paths = sorted(path for path in directory.rglob("*") if path.is_file())
    except OSError:
        return ()
    return paths


def scan_source(source: Source, stats: ScanStats) -> List[Event]:
    """Scan one endpoint and retain timestamped errors plus diagnostic INFO."""
    events = []
    for path in iter_log_files(source.directory):
        stats.files += 1
        try:
            with path.open(encoding="utf-8", errors="replace") as log_file:
                for line_number, raw_line in enumerate(log_file, start=1):
                    line = raw_line.rstrip()
                    level = classify_level(line)
                    if not ((level == "ERROR" and is_relevant(line)) or is_discriminative(line)):
                        continue
                    timestamp = parse_timestamp(line)
                    if timestamp is None:
                        if level == "ERROR":
                            stats.unparsed_relevant_errors += 1
                        continue
                    stats.timestamped_events += 1
                    events.append(Event(
                        timestamp, source, path, line_number, level, classify_module(line), line))
        except OSError:
            stats.unreadable_files += 1
    return events


def parse_source_spec(spec: str) -> Source:
    """Parse a NAME=PATH endpoint specification."""
    label, separator, directory = spec.partition("=")
    if not separator or not label or not directory:
        raise ValueError(f"Invalid --source '{spec}'; expected NAME=PATH.")
    return Source(label, Path(directory).expanduser())


def source_from_directory(directory: str, index: int) -> Source:
    """Use the directory name as a useful default endpoint label."""
    path = Path(directory).expanduser()
    label = path.name or f"source-{index}"
    return Source(label, path)


def source_directories_overlap(left: Path, right: Path) -> bool:
    """Reject roots that could scan one physical log line under two endpoint labels."""
    try:
        common = os.path.commonpath((str(left), str(right)))
    except ValueError:
        return False
    return common == str(left) or common == str(right)


def collect_sources(args: argparse.Namespace) -> List[Source]:
    """Build the input source list and reject ambiguous labels or paths."""
    sources = [parse_source_spec(spec) for spec in args.source]
    sources.extend(source_from_directory(directory, index)
                   for index, directory in enumerate(args.log_dirs, start=1))
    if not sources:
        sources = [Source("local", Path.home() / "ascend" / "log")]

    labels = set()
    validated_sources = []
    for source in sources:
        if source.label in labels:
            raise ValueError(f"Duplicate source label: {source.label}")
        if not source.directory.is_dir():
            raise ValueError(f"Log directory not found: {source.directory}")
        resolved_source = Source(source.label, source.directory.resolve())
        for existing in validated_sources:
            if source_directories_overlap(existing.directory, resolved_source.directory):
                raise ValueError(
                    f"Log sources overlap: {existing.label}={existing.directory} and "
                    f"{resolved_source.label}={resolved_source.directory}")
        labels.add(resolved_source.label)
        validated_sources.append(resolved_source)
    return validated_sources


def event_key(event: Event) -> Tuple[datetime, str, str, int]:
    """Provide a stable chronological ordering for events with equal timestamps."""
    return event.timestamp, event.source.label, str(event.path), event.line_number


def truncate(line: str, width: int = 500) -> str:
    """Keep timeline output readable without discarding the event identity."""
    return line if len(line) <= width else f"{line[:width - 3]}..."


def print_event(event: Event) -> None:
    """Emit one event with enough provenance to locate it again."""
    relative_path = event.path.relative_to(event.source.directory)
    logger.info(
        "[%s] [%s] [%s/%s] %s:%s",
        event.timestamp.isoformat(timespec="microseconds"),
        event.source.label,
        event.level,
        event.module,
        relative_path,
        event.line_number,
    )
    logger.info("  %s", truncate(event.line))


def print_context(event: Event, lines: int) -> None:
    """Emit local lines around an event without retaining every log in memory."""
    start = max(1, event.line_number - lines)
    end = event.line_number + lines
    try:
        with event.path.open(encoding="utf-8", errors="replace") as log_file:
            for line_number, line in enumerate(log_file, start=1):
                if line_number < start:
                    continue
                if line_number > end:
                    break
                prefix = ">" if line_number == event.line_number else " "
                logger.info("  %s%s: %s", prefix, line_number, truncate(line.rstrip()))
    except OSError:
        logger.info("  (Unable to read local context.)")


def print_comm_windows(events: Iterable[Event]) -> None:
    """Correlate HcclCommPrepare entries that share a communication domain."""
    comm_events: Dict[str, List[Event]] = defaultdict(list)
    for event in events:
        if "HcclCommPrepare" not in event.line:
            continue
        match = COMM_PATTERN.search(event.line)
        if match:
            comm_events[match.group(1)].append(event)

    windows = []
    for comm_id, grouped_events in comm_events.items():
        first_by_source = {}
        for event in sorted(grouped_events, key=event_key):
            first_by_source.setdefault(event.source.label, event)
        if len(first_by_source) < 2:
            continue
        ordered = sorted(first_by_source.values(), key=event_key)
        span = ordered[-1].timestamp - ordered[0].timestamp
        windows.append((ordered[0].timestamp, comm_id, ordered, span))

    if not windows:
        logger.info("(No shared HcclCommPrepare communication domain found.)")
        return
    for _, comm_id, grouped_events, span in sorted(windows):
        logger.info("comm[%s] cross-source span=%s", comm_id, span)
        for event in grouped_events:
            logger.info(
                "  %s: %s %s:%s",
                event.source.label,
                event.timestamp.isoformat(timespec="microseconds"),
                event.path.relative_to(event.source.directory),
                event.line_number,
            )


def print_limitations(sources: List[Source], stats: ScanStats, comm_events: Iterable[Event]) -> None:
    """State evidence limits that change how much certainty a diagnosis warrants."""
    if len(sources) == 1:
        logger.info(
            "- Only one log source is available; cross-endpoint root causes remain unverified.")
    else:
        logger.info(
            "- Cross-source ordering assumes the endpoint clocks are synchronized; verify NTP before "
            "treating small time differences as causal.")
    if stats.unparsed_relevant_errors:
        logger.info(
            "- %s HIXL-related ERROR line(s) had no supported timestamp "
            "and were excluded from chronological ordering.",
            stats.unparsed_relevant_errors,
        )
    if stats.unreadable_files:
        logger.info("- %s file(s) could not be read.", stats.unreadable_files)
    if not any("HcclCommPrepare" in event.line for event in comm_events):
        logger.info(
            "- No HcclCommPrepare entry was found; startup-skew analysis needs INFO-level HCCL logs.")


def parse_arguments() -> argparse.Namespace:
    """Define the small command-line surface used by log-triage.sh and agents."""
    parser = argparse.ArgumentParser(
        description="Chronologically merge HIXL-related events across plog directories.")
    parser.add_argument("--source", action="append", default=[], metavar="NAME=PATH",
                        help="label one endpoint explicitly; may be repeated")
    parser.add_argument("--top", type=int, default=12, help="maximum errors and INFO events to show")
    parser.add_argument("--context", type=int, default=2, help="context lines around the earliest error")
    parser.add_argument("log_dirs", nargs="*", help="unlabelled plog directories")
    return parser.parse_args()


def configure_logging() -> None:
    """Send triage report lines to stdout without timestamps or logger names."""
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)


def main() -> int:
    """Run a chronological, evidence-aware first-pass triage."""
    configure_logging()
    args = parse_arguments()
    if args.top < 1 or args.context < 0:
        logger.error("--top must be positive and --context cannot be negative.")
        return 2
    try:
        sources = collect_sources(args)
    except ValueError as error:
        logger.error("%s", error)
        return 2

    stats = ScanStats()
    events = sorted((event for source in sources for event in scan_source(source, stats)), key=event_key)
    errors = [event for event in events if event.level == "ERROR"]
    info_events = [event for event in events if event.level != "ERROR"]

    logger.info("=== HIXL chronological timeline ===")
    for source in sources:
        logger.info("source[%s]=%s", source.label, source.directory)
    logger.info(
        "Scanned %s file(s); retained %s timestamped event(s).",
        stats.files,
        stats.timestamped_events,
    )
    logger.info("")

    logger.info("--- Earliest HIXL-related ERROR ---")
    if errors:
        print_event(errors[0])
        logger.info("Context:")
        print_context(errors[0], args.context)
    else:
        logger.info("(No timestamped HIXL-related ERROR found.)")
    logger.info("")

    logger.info("--- Chronological HIXL-related ERRORs (first %s) ---", args.top)
    if errors:
        for event in errors[:args.top]:
            print_event(event)
    else:
        logger.info("(none)")
    logger.info("")

    logger.info("--- Discriminative INFO/WARN events (first %s) ---", args.top)
    if info_events:
        for event in info_events[:args.top]:
            print_event(event)
    else:
        logger.info("(none)")
    logger.info("")

    logger.info("--- Shared HcclCommPrepare windows ---")
    print_comm_windows(events)
    logger.info("")

    logger.info("--- Evidence limitations ---")
    print_limitations(sources, stats, events)
    return 0


if __name__ == "__main__":
    sys.exit(main())
