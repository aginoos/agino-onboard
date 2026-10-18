#!/usr/bin/env python3
"""Summarize complete-app comparisons without mixing first and warmed cycles.

Inputs use NAME=JSON; NAME should identify session and output scale. A passing
functional run is required. USS/PSS gates use each state's median separately
for cycle zero and later cycles, plus the aggregate. RSS remains a reported
aspirational metric. Raw ranges, sample counts and binary identities survive.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
from statistics import median

METRICS = ('rssKiB', 'ussKiB', 'pssKiB')
STATES = {'plain', 'closed', 'active-english', 'idle-english',
          'active-candidates', 'idle-candidates', 'active-expanded',
          'idle-expanded', 'active-settings', 'idle-settings', 'post-input-closed'}


def identity(sample):
    match = re.fullmatch(r'(qt|slint)-(\d+)-(.+)', sample['label'])
    if not match:
        raise ValueError('Unrecognized sample identity: ' + sample['label'])
    renderer, cohort, suffix = match.groups()
    cycle = sample.get('cycle')
    if (m := re.fullmatch(r'cycle-(\d+)-(.+)', suffix)):
        cycle, state = int(m[1]), m[2]
    elif (m := re.fullmatch(r'(plain|closed)-(\d+)', suffix)):
        state, cycle = m[1], int(m[2])
    else:
        state = suffix
    phase = ('initial' if state == 'cold-hidden' else
             'first-cycle' if cycle == 0 else
             'later-cycles' if cycle is not None and cycle > 0 else 'unspecified')
    return renderer, int(cohort), state, phase


def summarize(name, path):
    report = json.loads(path.read_text())
    if report.get('result') != 'pass':
        raise ValueError(f'{name}: incomplete or unsuccessful run')
    if report.get('diagnosticPlainOnly') or report.get('finalAcceptance') is False:
        raise ValueError(f'{name}: diagnostic or canceled run cannot pass release gates')
    groups = defaultdict(lambda: defaultdict(list))
    coverage = defaultdict(lambda: defaultdict(lambda: defaultdict(int)))
    for sample in report['samples']:
        if any(sample['totals'][k] != 0 for k in ('swapKiB', 'swapPssKiB', 'vmSwapKiB')):
            raise ValueError(f'{name}: swapped memory invalidates comparison')
        renderer, cohort, state, phase = identity(sample)
        coverage[(renderer, cohort)][phase][state] += 1
        for category in (phase, 'all-cycles'):
            groups[(category, state)][renderer].append(sample)
    if not coverage:
        raise ValueError(f'{name}: empty report')
    for cohort, phases in coverage.items():
        if dict(phases.get('initial', {})) != {'cold-hidden': 1}:
            raise ValueError(f'{name}: missing cold-hidden sample for {cohort}')
        if dict(phases.get('first-cycle', {})) != dict.fromkeys(STATES, 1):
            raise ValueError(f'{name}: incomplete first input cycle for {cohort}')
        later = phases.get('later-cycles', {})
        if later and (set(later) != STATES or len(set(later.values())) != 1):
            raise ValueError(f'{name}: incomplete later input cycles for {cohort}')
        if 'cycles' in report and (report['cycles'] < 1 or
                later.get('plain', 0) != report['cycles'] - 1):
            raise ValueError(f'{name}: missing declared input cycles for {cohort}')
        if set(phases) - {'initial', 'first-cycle', 'later-cycles'}:
            raise ValueError(f'{name}: unrecognized phase for {cohort}')
    rows = []
    failures = []
    for (phase, state), renderers in sorted(groups.items()):
        if set(renderers) != {'qt', 'slint'}:
            raise ValueError(f'{name}: unmatched comparison {phase}/{state}')
        if len(renderers['qt']) != len(renderers['slint']):
            raise ValueError(f'{name}: unequal sample counts for {phase}/{state}')
        row = dict(phase=phase, state=state)
        for renderer, samples in renderers.items():
            row[renderer] = dict(samples=len(samples), metrics={})
            for metric in METRICS:
                values = [s['totals'][metric] for s in samples]
                row[renderer]['metrics'][metric] = dict(median=median(values), minimum=min(values), maximum=max(values))
        row['targetMet'] = all(row['qt']['metrics'][k]['median'] <= row['slint']['metrics'][k]['median'] for k in ('ussKiB', 'pssKiB'))
        if not row['targetMet']:
            failures.append(dict(phase=phase, state=state,
                                 deltaKiB={k: row['qt']['metrics'][k]['median'] - row['slint']['metrics'][k]['median'] for k in ('ussKiB', 'pssKiB')}))
        rows.append(row)
    return dict(name=name, input=str(path), binaries=report['binaries'],
                targetMet=not failures, failures=failures, rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', action='append', required=True, help='NAME=JSON')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = dict(schema=1, units='KiB', criteria='per-state first/later/all cycle median Qt USS and PSS <= Slint; no swap', comparisons=[])
    try:
        for entry in args.input:
            name, path = entry.split('=', 1)
            result['comparisons'].append(summarize(name, Path(path)))
    except (ValueError, KeyError) as error:
        parser.exit(2, str(error) + '\n')
    result['targetMet'] = all(r['targetMet'] for r in result['comparisons'])
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(dict(targetMet=result['targetMet'], failures={r['name']: r['failures'] for r in result['comparisons']}), ensure_ascii=False))
    return 0 if result['targetMet'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
