#! /usr/bin/env python3

"""
Download test artifacts from ATI (Android Test Investigate).

This script downloads imgdiag artifacts for specified ATI test runs.

Usage:
    # Download artifacts from specific runs:
    ./ati_download_artifacts.py --invocation-id I79100010355578895 --invocation-id I84900010357346481

    # Download latest 10 imgdiag runs:
    ./ati_download_artifacts.py --imgdiag-atp 10

    # Check all command line flags:
    ./ati_download_artifacts.py --help
"""

import tempfile
import argparse
import subprocess
import time
import os
import concurrent.futures
import json

try:
  from tqdm import tqdm
except:
  def tqdm(x, *args, **kwargs):
    return x

LIST_ARTIFACTS_QUERY = """
SELECT
  CONCAT('https://android-build.corp.google.com/builds/', invocation_id, '/', name) AS url
FROM android_build.testartifacts.latest
WHERE invocation_id = '{invocation_id}';
"""

LIST_IMGDIAG_RUNS_QUERY = """
SELECT t.invocation_id,
 t.test.name,
 t.timing.creation_timestamp
FROM android_build.invocations.latest AS t
WHERE t.test.name like '%imgdiag_top_100%'
ORDER BY t.timing.creation_timestamp DESC
LIMIT {};
"""

REQUIRED_IMGDIAG_FILES = [
  "combined_imgdiag_data",
  "all-dirty-objects",
  "dirty-image-objects-art",
  "dirty-image-objects-framework",
  "dirty-page-counts",
]

def filter_artifacts(artifacts):
  return [a for a in artifacts if any(x in a for x in REQUIRED_IMGDIAG_FILES)]

def list_last_imgdiag_runs(run_count):
  query_file = tempfile.NamedTemporaryFile()
  out_file = tempfile.NamedTemporaryFile()
  with open(query_file.name, 'w') as f:
    f.write(LIST_IMGDIAG_RUNS_QUERY.format(run_count))
  cmd = f'f1-sql --input_file={query_file.name} --output_file={out_file.name} --csv_output --print_queries=false'
  res = subprocess.run(cmd, shell=True, check=True, capture_output=True)
  with open(out_file.name) as f:
    content = f.read()
  content = content.split()[1:]
  for i in range(len(content)):
    content[i] = content[i].replace('"', '').split(',')
  return content

def list_artifacts(invocation_id):
  if not invocation_id:
    raise ValueError(f'Invalid invocation: {invocation_id}')

  query_file = tempfile.NamedTemporaryFile()
  out_file = tempfile.NamedTemporaryFile()
  with open(query_file.name, 'w') as f:
    f.write(LIST_ARTIFACTS_QUERY.format(invocation_id=invocation_id))
  cmd = f'f1-sql --input_file={query_file.name} --output_file={out_file.name} --csv_output --print_queries=false --quiet'
  execute_command(cmd)
  with open(out_file.name) as f:
    content = f.read()
  content = content.split()
  content = content[1:]
  for i in range(len(content)):
    content[i] = content[i].replace('"', '')
  return content

def execute_command(cmd):
  for i in range(5):
    try:
      subprocess.run(cmd, shell=True, check=True)
      return
    except Exception as e:
      print(f'Failed to run: {cmd}\nException: {e}')
      time.sleep(2 ** i)

  raise RuntimeError(f'Failed to run: {cmd}')

def download_artifacts(res_dir, artifacts):
  os.makedirs(res_dir, exist_ok=True)

  commands = []
  for url in artifacts:
    filename = url.split('/')[-1]
    out_path = os.path.join(res_dir, filename)
    cmd = f'sso_client {url} --connect_timeout=120 --dns_timeout=120 --request_timeout=600 --location > {out_path}'
    commands.append(cmd)

  if not commands:
    return

  with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
    res = list(tqdm(executor.map(execute_command, commands), total=len(commands), leave=False))


def download_invocations(args, invocations):
  for invoc_id in tqdm(invocations):
    artifacts = list_artifacts(invoc_id)
    if not args.download_all:
      artifacts = filter_artifacts(artifacts)

    res_dir = os.path.join(args.out_dir, invoc_id)
    download_artifacts(res_dir, artifacts)


def main():
  parser = argparse.ArgumentParser(
    description='Download artifacts from ATI',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
  )
  parser.add_argument(
    '--out-dir',
    default='./',
    help='Output dir for downloads',
  )
  parser.add_argument(
    '--invocation-id',
    action='append',
    default=None,
    help='Download artifacts from the specified invocations',
  )
  parser.add_argument(
    '--imgdiag-atp',
    metavar='N',
    dest='atp_run_count',
    type=int,
    default=None,
    help='Download latest N imgdiag runs',
  )
  parser.add_argument(
    '--download-all',
    action=argparse.BooleanOptionalAction,
    default=False,
    help='Whether to download all artifacts or combined imgdiag data only',
  )
  parser.add_argument(
    '--overwrite',
    action=argparse.BooleanOptionalAction,
    default=False,
    help='Download artifacts again even if the invocation_id dir already exists',
  )
  args = parser.parse_args()
  if not args.invocation_id and not args.atp_run_count:
    print('Must specify at least one of: --invocation-id or --imgdiag-atp')
    return

  invocations = set()
  if args.invocation_id:
    invocations.update(args.invocation_id)
  if args.atp_run_count:
    recent_runs = list_last_imgdiag_runs(args.atp_run_count)
    invocations.update({invoc_id for invoc_id, name, timestamp in recent_runs})

  if not args.overwrite:
    existing_downloads = set()
    for invoc_id in invocations:
      res_dir = os.path.join(args.out_dir, invoc_id)
      if os.path.isdir(res_dir):
        existing_downloads.add(invoc_id)

    if existing_downloads:
      print(f'Skipping existing downloads: {existing_downloads}')
      invocations = invocations - existing_downloads

  print(f'Downloading: {invocations}')
  download_invocations(args, invocations)


if __name__ == '__main__':
  main()
