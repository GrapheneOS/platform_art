#! /usr/bin/env python3

"""
Compare private dirty page counts between imgdiag test runs.

This script compares private dirty page counts in the boot-image object section
between specified imgdiag runs.

Usage:
    # Compare multiple imgdiag runs:
    ./compare_imgdiag_runs.py ./I1234 ./I1235 ./I1236

    # Check all command line flags:
    ./compare_imgdiag_runs.py --help
"""

import argparse
import glob
import gzip
import json
import os
import pprint
import statistics

"""
These thresholds are used to verify that process sets from different runs
are similar enough for meaningful comparison. Constants are based on
the imgdiag run data of 2025-01. Update if necessary.
"""

# There are about 100 apps in imgdiag_top_100 ATP config + more than 100 system processes.
MIN_COMMON_PROC_COUNT = 200
# Allow for a relatively small (<20%) mismatch in process sets between runs.
MAX_MISMATCH_PROC_COUNT = 40

def main():
  pp = pprint.PrettyPrinter(indent=2)
  parser = argparse.ArgumentParser(
    description='Compare private dirty page counts between imgdiag ATP runs',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
  )

  parser.add_argument(
    'invocation_dirs',
    nargs='+',
    help='Directories with imgdiag output files',
  )

  parser.add_argument(
    '--verbose',
    action=argparse.BooleanOptionalAction,
    default=False,
    help='Print out all mismatched processes and more info about dirty page diffs between individual processes',
  )
  args = parser.parse_args()

  runs = []
  for invoc_dir in args.invocation_dirs:
    res = glob.glob(os.path.join(invoc_dir, '*dirty-page-counts*'))
    if len(res) != 1:
      raise ValueError(f"Expected to find exactly one *dirty-page-counts* file in {invoc_dir}, but found: {res}")

    try:
      if res[0].endswith('.gz'):
          with gzip.open(res[0], 'rb') as f:
            contents = json.load(f)
      else:
        with open(res[0], 'r') as f:
          contents = json.load(f)
    except:
      print('Failed to read: ', res[0])
      raise
    runs.append(contents)

  basename = lambda p: os.path.basename(os.path.normpath(p))
  invoc_ids = [basename(path) for path in args.invocation_dirs]
  print('Comparing: ', invoc_ids)

  items = list()
  for r in runs:
    items.append({k[:k.rfind('_')]: v for k, v in r.items()})

  proc_names = list(set(i.keys()) for i in items)
  common_proc_names = set.intersection(*proc_names)
  mismatch_proc_names = set.union(*proc_names) - common_proc_names
  print('Common proc count (used in the comparison): ', len(common_proc_names))
  if len(common_proc_names) < MIN_COMMON_PROC_COUNT:
      print('WARNING: common processes count is too low.')
      print(f'Should be at least {MIN_COMMON_PROC_COUNT}.')

  print('Mismatching proc count (not present in all runs): ', len(mismatch_proc_names))
  if len(mismatch_proc_names) > MAX_MISMATCH_PROC_COUNT:
      print('WARNING: too many mismatching process names.')
      print(f'Should be lower than {MAX_MISMATCH_PROC_COUNT}.')

  if args.verbose:
    print("Mismatching process names:")
    pp.pprint(mismatch_proc_names)

  dirty_page_sums = list()
  for r in items:
    dirty_page_sums.append(sum(r[k] for k in common_proc_names))

  print(f'Total dirty pages:\n{dirty_page_sums}\n')

  mean_dirty_pages = [s / len(common_proc_names) for s in dirty_page_sums]
  print(f'Mean dirty pages:\n{mean_dirty_pages}\n')

  median_dirty_pages = [statistics.median(r[name] for name in common_proc_names) for r in items]
  print(f'Median dirty pages:\n{median_dirty_pages}\n')

  if args.verbose:
    print(f'Largest dirty page diffs:')
    for i in range(len(invoc_ids)):
      for j in range(len(invoc_ids)):
        if j <= i:
          continue

        page_count_diffs = [(proc_name, items[i][proc_name], items[j][proc_name], items[j][proc_name] - items[i][proc_name]) for proc_name in common_proc_names]
        page_count_diffs = sorted(page_count_diffs, key=lambda x: abs(x[3]), reverse=True)
        print(f'Between {invoc_ids[i]} and {invoc_ids[j]}: ')
        pp.pprint(page_count_diffs[:10])


if __name__ == '__main__':
  main()
