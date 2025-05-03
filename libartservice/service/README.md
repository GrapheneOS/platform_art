# ART Service

Warning: The contents in this doc can become stale while the code evolves.

ART Service manages dexopt artifacts of apps. With ART Service, you can dexopt
apps, query their dexopt status (the compiler filter, the compilation reason,
whether the dexopt artifacts are up-to-date, etc.), and delete dexopt artifacts.

Note: ART Service is introduced in Android 14. Prior to ART Service, dexopt
artifacts were managed by Package Manager with a legacy implementation. The
legacy implementation will be removed in future releases. This doc only
describes ART Service, not the legacy implementation.

## Dexopt

*Dexopt* stands for the procedure of ahead-of-time (AOT) DEX optimization.
Specifically, at the time of writing, it includes

-   *Compilation*: Compiles the DEX code into native code.
-   *Verification*: Verifies the DEX code against Java verification rules and
    persists verification metadata.
-   *Class resolution & initialization*: Resolves classes in the DEX code into
    ART's internal data structures and persists a memory dump of the data
    structures. For classes that can be statically initialized (i.e., the static
    initializer can be executed at dexopt time), also initializes the classes
    and saves the result.
-   *Extraction*: If the DEX code is compressed, decompresses it and persists
    the uncompressed copy.

## Glossary

-   *Dexopt*: The procedure of ahead-of-time (AOT) DEX optimization, as
    described above.
-   *dex2oat*: The tool to perform dexopt.
-   *Dexpreopt*: Dexopt performed at build time on build host (when the Android
    system image is being built). This is not in the scope of ART Service, as
    ART Service runs on device.
-   *DEX files*: The files that contain DEX code, including APKs, JARs, and
    plain DEX files. Strictly speaking, an APK/JAR file is not a DEX file. It is
    a ZIP file that contain one or more plain DEX files. However, it is called a
    *DEX file* conventionally.

## Concepts

### Compiler filters

One core ART option to configure is the compiler filter. The compiler filter
drives how ART dexopts DEX code and is an option passed to the `dex2oat` tool.
At the time of writing, there are three officially supported filters:

-   `verify`: Performs only *verification* and *extraction* (no *compilation* or
    *class resolution & initialization*).
-   `speed`: Performs *verification* and *extraction*, and performs
    *compilation* for all methods (no *class resolution & initialization*).
-   `speed-profile`: Performs *verification* and *extraction*, performs
    *compilation* for methods listed in the profile, and performs *class
    resolution & initialization* for classes listed in the profile.

### Dexopt dependencies

To dexopt a DEX file, dex2oat needs to know all its dependencies. This includes
bootclasspath jars, boot images, and class loader context (CLC, consisting of
shared libraries and other splits of the same app).

When the runtime loads dexopt artifacts at execution time, it performs a check
on the dexopt-time dependencies against the actual runtime dependencies. The
dexopt-time dependencies and the runtime dependencies must exactly match.

In the case of a dependency mismatch, the runtime is unable to use the result of
*compilation* and *class resolution & initialization*. I.e., the result of
*verification* and *extraction* can still be used, and dexopt artifacts are used
as if the DEX file in the `verify` state, regardless of the actual compiler
filter (see [The `vdex` reason](#the-reason)).

### Priority classes

A priority class indicates the priority of an operation. The value affects the
resource usage (e.g., CPU cores) and the process priority. A higher value may
result in faster execution but may consume more resources and compete for
resources with other processes.

Options are (from the highest to the lowest):

-   `PRIORITY_BOOT`: Indicates that the operation blocks boot.
-   `PRIORITY_INTERACTIVE_FAST`: Indicates that a human is waiting on the result
    and the operation is more latency sensitive than usual. It's typically used
    when the user is entirely blocked, such as for restoring from cloud backup.
-   `PRIORITY_INTERACTIVE`: Indicates that a human is waiting on the result
    (e.g., for app install).
-   `PRIORITY_BACKGROUND`: Indicates that the operation runs in background.

### Compilation reasons

A compilation reason is a string that determines the default
[compiler filter](#compiler-filters) and the default
[priority class](#priority-classes) of an operation.

It's also passed to `dex2oat` and stored in the header of the OAT file, for
debugging purposes. To retrieve the compilation reason from an OAT file, run

```
pm art dump <package-name>
```

or

```
oatdump --header-only --oat-file=<odex-filename> | grep 'compilation-reason ='
```

It can be either a predefined value in
`art/libartservice/service/java/com/android/server/art/ReasonMapping.java` or a
custom string. If the value is a custom string, the priority class and the
compiler filter must be explicitly set.

Each predefined value corresponds to one of the
[dexopt scenarios](#dexopt-scenarios).

#### The `vdex` reason

The `vdex` reason is a special *compilation reason* indicating that a dex file
is dexopted but there is a dependency mismatch. You may see it in the dexopt
state dump (`pm art dump [<package-name>]` or `dumpsys package dexopt`), as
`[reason=vdex]`). It is not an actual compilation reason passed to `dex2oat` or
stored in the OAT header.

It typically occurs when:

-   An app is a user-installed app, and the app store delivered **uncompressed
    DEX code** and **verification metadata** on installation, and
    -   the app store didn't deliver a profile (for Play Store, this happens
        during the first few hours after the app is published by an app
        developer), or
    -   dexopt on install was explicitly skipped by the app store, by setting
        the install scenario to `INSTALL_SCENARIO_FAST`, which is translated to
        the `install-fast` compilation reason, whose default behavior is to skip
        dexopt, or
    -   dexopt on install was skipped because the app store installed the app
        through [incremental install](http://go/incremental-in-android).
-   An app is a pre-installed app, and dexpreopt was performed with the wrong
    dependencies. This is not unusual because dexpreopt has many known issues.
-   The device just installed a Mainline update, which updated the dependencies,
    and
    -   (platform is Android 14 or below) Pre-reboot Dexopt didn't run because
        it's not supported, or
    -   (platform is Android 15 or above) Pre-reboot Dexopt didn't complete
        before the reboot because there wasn't enough idle and charging time for
        it to complete.
-   The device just installed an OTA update, which updated the dependencies, and
    -   (platform before update was Android 14 or below) *otapreopt* ran but
        failed (this is not unusual because *otapreopt* has many known issues),
        or
    -   (platform before update was Android 16 or above) Pre-reboot Dexopt
        didn't complete before the reboot because there wasn't enough idle and
        charging time for it to complete.

#### The `-dm` suffix

Sometimes, you may see the `-dm` suffix in the compilation reason stored in an
OAT header, such as `install-dm`. However, the `-dm` suffix is **not** a part of
the compilation reason. It's appended to the compilation reason to indicate that
a DM (`.dm`) file is passed to `dex2oat` during dexopt for **app install**.

Note: ART Service also passes the DM file to `dex2oat` in other scenarios, such
as background dexopt, but for compatibility reasons, the `-dm` suffix is not
appended in those scenarios.

Note: The `-dm` suffix does **not** imply anything in the DM file being used by
`dex2oat`. The augmented compilation reason can still be `install-dm` even if
the DM file is empty or if `dex2oat` leaves all contents of the DM file unused.
That would only happen if there's a bug, like the wrong DM file being passed.

### Primary dex vs. secondary dex

ART Service dexopts both primary dex files and secondary dex files of an app.

A primary dex file refers to the base APK or a split APK of an app. It's
installed by Package Manager or shipped as a part of the system image, and it's
loaded by Framework on app startup.

A secondary dex file refers to an APK or JAR file that an app adds to its own
data directory and loads dynamically.

## Dexopt scenarios

At a high level, ART Service dexopts apps in the following scenarios:

-   the device is on the very first boot (Compilation reason: `first-boot`)
-   the device is on the first boot after an OTA update (Compilation reason:
    `boot-after-ota`)
-   the device is on the first boot after a mainline update (Compilation reason:
    `boot-after-mainline-update`)
-   an app is being installed (Compilation reason: `install` / `install-bulk` /
    etc.)
-   the device is idle and charging (Compilation reason: `bg-dexopt` /
    `inactive`)
-   the device has a pending OTA / Mainline update and is idle and charging
    (Compilation reason: `ab-ota`)
-   requested through commandline (Compilation reason: `cmdline`)

Warning: The execution or scheduling of dexopt operations by ART Service is
**not** triggered by an app's running or launch status.

Warning: The sections below describe the default behavior in each scenario. Note
that the list of apps to dexopt and the compiler filter, as well as other
options, can be customized by partners through system properties, APIs, etc.

### On the very first boot / the first boot after an OTA update

On the very first boot / the first boot after an OTA update, ART Service only
dexopts primary dex files of all apps [[1]](#1) with the `verify` compiler
filter.

Note: It doesn't dexopt secondary dex files or use the `speed-profile` filter
because doing so may block the boot for too long.

However, this doesn't mean all apps are in the `verify` state on the first boot
/ after OTA. In fact:

-   On the first boot,
    -   if an app has a profile on the Android source tree (see
        [doc](http://go/art-app-profiles#using-the-profile-on-an-android-source-tree)),
        the app is in `speed-profile`, dexopted by dexpreopt at build time;
    -   if an app is added to `PRODUCT_DEXPREOPT_SPEED_APPS`, the app is in
        `speed`, dexopted by dexpreopt at build time;
    -   otherwise, the app is in `verify`.
-   On boot after OTA,
    -   if
        [Pre-reboot Dexopt](#when-the-device-has-a-pending-ota-mainline-update-pre_reboot-dexopt)
        (formerly *otapreopt*) ran correctly and completed before the reboot,
        apps are in `speed-profile`;
    -   otherwise, apps are in `verify`.

In practice, ART Service **does nothing** for most of the apps in this scenario.
Because the default compiler filter is `verify`, which tolerates dependency
mismatches, apps with usable VDEX files generally don't need to be re-dexopted.
This includes:

-   apps on the **system partitions** that have artifacts generated by
    dexpreopt, even if the dependencies (class loader contexts) are not properly
    configured.
-   apps on the **data partition** that have been dexopted in other scenarios
    (install, background dexopt, etc.), even though their dependencies
    (bootclasspath, boot images, and class loader contexts) have probably
    changed.

In other words, in this scenario, ART Service mostly only dexopts:

-   apps in APEXes, because they are not supported by dexpreopt
-   apps on the system partitions with dexpreopt disabled
-   apps forced to have `speed-profile` or `speed` compiler filters (the system
    UI and the launcher) but dexpreopted with wrong dependencies

### On the first boot after a mainline update

On the first boot after a mainline update, ART Service dexopts the primary dex
files of the system UI and the launcher. It uses the compiler filter specified
by `dalvik.vm.systemuicompilerfilter` [[2]](#2) for the system UI, and uses the
`speed-profile` compiler filter for the launcher.

Note: It only dexopts those two apps because they are important to user
experience.

However, this doesn't mean all other apps are in the `verify` state on the first
boot after a mainline update. In fact,

-   if Pre-reboot Dexopt ran correctly and completed before the reboot, apps are
    in `speed-profile`;
-   otherwise, apps are in `verify`.

### During app installation

During app installation, ART Service dexopts the primary dex files [[3]](#3) of
the app. If the app is installed along with a DM file that contains a profile
(known as a *cloud profile*), it uses the `speed-profile` compiler filter.
Otherwise, it uses the `verify` compiler filter.

This procedure is by default performed, unless

-   explicitly skipped by the app store, by setting the install scenario to
    `INSTALL_SCENARIO_FAST`, which is translated to the `install-fast`
    compilation reason, whose default behavior is to skip dexopt, or
-   skipped because the app store installed the app through
    [incremental install](http://go/incremental-in-android).

To skip *extraction*, an app store can deliver uncompressed DEX. To skip
*verification*, an app store can perform Cloud Verification on the server side
and deliver verification metadata.

Note: This means, if an app store delivers **uncompressed DEX code**,
**verification metadata**, and no profile on installation, then the app is
already in the `verify` state and the target compiler filter is `verify`, so no
dexopt will be performed.

### When the device is idle and charging (background dexopt)

ART Service has a job called *background dexopt job*, managed by Job Scheduler.
It is triggered daily when the device is idle and charging. During the job
execution, it dexopts primary dex files and secondary dex files of all apps
[[1]](#1) with the `speed-profile` compiler filter.

The job is cancellable. When the device is no longer idle or charging, Job
Scheduler cancels the job.

It means serves two purposes:

-   After sufficient information is collected in the local profile for an app
    based on the user's use pattern, the background dexopt job re-dexopts the
    app with the local profile combined with the cloud profile, to generate
    dexopt artifacts tailored for the user.
-   After an OTA / Mainline update, if an app was not re-dexopted by Pre-reboot
    Dexopt, the background dexopt job re-dexopts it.

### When the device has a pending OTA / Mainline update (Pre-reboot Dexopt)

An OTA / Mainline update almost always deliver changes to apps' dependencies, so
we cannot use the same dexopt artifacts after an update, or there will be a
dependency mismatch. Therefore, we need a procedure to re-dexopt apps using the
dependencies contained in the update. *Pre-reboot Dexopt* is for this purpose.

When an OTA / Mainline update is downloaded and pending, *Pre-reboot Dexopt*
dexopts primary dex files and secondary dex files of all apps [[1]](#1) for the
update with the `speed-profile` compiler filter, before the reboot.

Pre-reboot Dexopt was introduced in Android 15. Prior to that, there was
*otapreopt*, performing a similar operation but having the following
limitations:

-   It only supports OTA updates, not Mainline updates.
-   It has many known issues. For example, if the platform before the OTA update
    is Android 14 (excluding Android 14 QPR1 and above), it cannot invoke
    `dex2oat` due to a known issue.
-   Its efficiency is not ideal. If the platform before the OTA update is
    Android 12~14 (excluding Android 14 QPR1 and above), it typically takes
    around an hour, whereas Pre-reboot Dexopt typically takes around 13 minutes.

#### Asynchronous Pre-reboot Dexopt

Asynchronous Pre-reboot Dexopt does not block the update installation. It is run
by a job called *Pre-reboot Dexopt job*, managed by Job Scheduler, triggered
once when the device is idle and charging. The user can reboot the device to
apply the update at any time, even if Pre-reboot Dexopt has not completed yet.

After applying the update, apps that were not dexopted by Pre-reboot Dexopt are
in the `verify` state. They can still run normally, being interpreted or JITed,
and will be dexopted by background dexopt, when the device is idle and charging.

This is opposed to synchronous Pre-reboot Dexopt, which blocks the update
installation. It is guaranteed to complete, but the drawback is that the user
cannot reboot the device to apply the update until it completes. For users who
are eager to try a new version of Android or urgently needs to apply a security
fix, this is frustrating.

There is no perfect solution, only trade-offs. We made Pre-reboot Dexopt
asynchronous because we believe this delivers a better user experience.

-   For Mainline updates, Pre-reboot Dexopt has already been asynchronous since
    introduced.
-   For OTA updates, Pre-reboot Dexopt was synchronous in Android 15, and is
    asynchronous since Android 16.

### When requested through commandline

ART Service can be invoked by commands (`pm compile`, `pm bg-dexopt-job`, and
`pm art dexopt-packages`). Run `pm help` to see the usages and the differences
between them.

## Notes

### [1]

If `pm.dexopt.downgrade_after_inactive_days` is set (typically on low-end
devices), ART Service only dexopts apps used within the last given number of
days. In addition, during background dexopt, if the device's remaining disk
space is low, ART Service downgrades other apps (with the compilation reason
`inactive`, whose default compiler filter is `verify`).

### [2]

Typically, `dalvik.vm.systemuicompilerfilter` is set to `speed`. This is because
the System UI package is typically added to `PRODUCT_DEXPREOPT_SPEED_APPS`,
dexpreopted using the `speed` compiler filter, and therefore it's never JITed
and as a result there is no profile collected on the device to use. This may
change in the future, but for now, we strongly recommend to set
`dalvik.vm.systemuicompilerfilter` to `speed`.

### [3]

There is no secondary dex file present during installation.
