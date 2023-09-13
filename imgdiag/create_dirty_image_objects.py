#! /usr/bin/env python3
#
# Copyright 2023, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
from collections import defaultdict
import os


def process_dirty_entries(entries, with_sort):
  mark_counts = defaultdict(int)
  dirty_image_objects = []

  union = set()
  for v in entries.values():
    union = union.union(v)

  for obj in union:
    str_marker = ''
    marker = 0
    # Sort marker is uint32_t, where Nth bit is set if Nth process has this object dirty.
    for idx, v in enumerate(entries.values()):
      if obj in v:
        str_marker += chr(ord('A') + idx)
        marker = (marker << 1) | 1
      else:
        str_marker += '_'
        marker = marker << 1

    if with_sort:
      dirty_image_objects.append(obj + ' ' + str(marker) + '\n')
    else:
      dirty_image_objects.append(obj + '\n')

    mark_counts[str_marker] += 1

  return (dirty_image_objects, mark_counts)


def main():
  parser = argparse.ArgumentParser(
      description=(
          'Create dirty-image-objects file from specified imgdiag output files.'
      ),
      formatter_class=argparse.ArgumentDefaultsHelpFormatter,
  )
  parser.add_argument(
      'imgdiag_files',
      nargs='+',
      help='imgdiag files to use.',
  )
  parser.add_argument(
      '--sort-objects',
      action=argparse.BooleanOptionalAction,
      default=True,
      help='Use object sorting.',
  )
  parser.add_argument(
      '--output-filename',
      default='dirty-image-objects.txt',
      help='Output file for dirty image objects.',
  )
  parser.add_argument(
      '--print-stats',
      action=argparse.BooleanOptionalAction,
      default=False,
      help='Print dirty object stats.',
  )

  args = parser.parse_args()

  entries = dict()
  for path in args.imgdiag_files:
    with open(path) as f:
      lines = f.readlines()
    prefix = 'dirty_obj: '
    lines = [l.strip().removeprefix(prefix) for l in lines if prefix in l]
    entries[path] = set(lines)

  if args.sort_objects and len(entries) > 32:
    print(
        'WARNING: too many processes for sorting, using top 32 by number of'
        ' dirty objects.'
    )
    entries_list = sorted(
        list(entries.items()), reverse=True, key=lambda x: len(x[1])
    )
    entries_list = entries_list[0:32]
    entries = {k: v for (k, v) in entries_list}

  print('Using processes:')
  for k, v in sorted(entries.items(), key=lambda x: len(x[1])):
    print(f'{k}: {len(v)}')
  print()

  dirty_image_objects, mark_counts = process_dirty_entries(
      entries=entries, with_sort=args.sort_objects
  )

  with open(args.output_filename, 'w') as f:
    f.writelines(dirty_image_objects)

  if args.print_stats:
    mark_counts = sorted(
        list(mark_counts.items()), key=lambda x: x[1], reverse=True
    )

    for i, path in enumerate(entries.keys()):
      print(path, chr(ord('A') + i))

    total_count = 0
    for marker, count in mark_counts:
      print(marker, count)
      total_count += count
    print('total: ', total_count)


if __name__ == '__main__':
  main()
