# ART Service internal doc

Warning: The contents in this doc can become stale while the code evolves.

## Pre-reboot Dexopt

Pre-reboot Dexopt is a successor of otapreopt, available on Android V+.

### On Mainline update

On Mainline update, `ArtManagerLocal.onApexStaged` is called. The method
schedules an asynchronous job and returns immediately. Later, when the device is
idle and charging, the job will be run by the job scheduler.

### On OTA update

On Mainline update, the shell command `pm art on-ota-staged` is called. The
behavior depends on the platform version and the configuration.

- On Android V

  By default, Pre-reboot Dexopt runs in synchronous mode from the postinstall
  script while update_engine keeps the snapshot devices mapped. The command
  blocks until Pre-reboot Dexopt finishes.

- On Android V, with asynchronous mode enabled

  The asynchronous mode can be enabled by
  `dalvik.vm.pr_dexopt_async_for_ota=true`. In this case, the command schedules
  an asynchronous job and returns immediately. Later, when the device is idle
  and charging, the job will be run by the job scheduler. The job uses
  `snapshotctl` to map snapshot devices.

  Note that this mode has a risk of racing with update_engine on snapshot
  devices. Particularly, if update_engine wants to unmap snapshot devices, to
  revoke an OTA update, the job may be running and preventing update_engine from
  successfully doing so.

- On Android B+

  Pre-reboot Dexopt is always in asynchronous mode. The command schedules an
  asynchronous job and returns immediately. Later, when the device is idle and
  charging, the job will be run by the job scheduler. The job will call
  `UpdateEngine.triggerPostinstall` to ask update_engine to map snapshot
  devices, and update_engine will call this command again with '--start' through
  the postinstall script, to notify the job that the snapshot devices are ready.

### On shell command

Pre-reboot Dexopt can be triggered by a shell command `pm art pr-dexopt-job`.
It is synchronous if called with `--run`, and is asynchronous if called with
`--schedule`.

Regardless of being synchronous or asynchronous, it always tries to map snapshot
devices if called with `--slot`. On Android V, it does so through `snapshotctl`,
and on Android B+, it does so through `UpdateEngine.triggerPostinstall`.
