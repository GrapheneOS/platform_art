#!/usr/bin/env lucicfg
#
# Copyright (C) 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""LUCI project configuration for the production instance of LUCI.

After modifying this file execute it ('./main.star') to regenerate the configs.
"""

lucicfg.check_version("1.30.9", "Please update depot_tools")

luci.builder.defaults.experiments.set({
    "luci.recipes.use_python3": 100,
})

# Use LUCI Scheduler BBv2 names and add Scheduler realms configs.
lucicfg.enable_experiment("crbug.com/1182002")

# Tell lucicfg what files it is allowed to touch.
lucicfg.config(
    config_dir = "generated",
    fail_on_warnings = True,
    lint_checks = ["default"],
)

# TODO: Switch to project-scoped service account.

luci.project(
    name = "art",
    buildbucket = "cr-buildbucket.appspot.com",
    logdog = "luci-logdog.appspot.com",
    milo = "luci-milo.appspot.com",
    notify = "luci-notify.appspot.com",
    scheduler = "luci-scheduler.appspot.com",
    swarming = "chromium-swarm.appspot.com",
    acls = [
        acl.entry(
            roles = [
                acl.BUILDBUCKET_READER,
                acl.LOGDOG_READER,
                acl.PROJECT_CONFIGS_READER,
                acl.SCHEDULER_READER,
            ],
            groups = "googlers",
        ),
        acl.entry(
            roles = [
                acl.BUILDBUCKET_OWNER,
                acl.SCHEDULER_OWNER,
            ],
            groups = "project-art-admins",
        ),
        acl.entry(
            roles = acl.LOGDOG_WRITER,
            groups = "luci-logdog-chromium-writers",
        ),
    ],
    bindings = [
        luci.binding(
            roles = "role/swarming.poolOwner",
            groups = "project-art-admins",
        ),
        luci.binding(
            roles = "role/swarming.poolViewer",
            groups = "googlers",
        ),
    ],
)

# Per-service tweaks.
luci.logdog(gs_bucket = "chromium-luci-logdog")
luci.milo(logo = "https://storage.googleapis.com/chrome-infra-public/logo/art-logo.png")

# Allow admins to use LED and "Debug" button on every builder and bot.
luci.binding(
    realm = "@root",
    roles = "role/swarming.poolUser",
    groups = "project-art-admins",
)
luci.binding(
    realm = "@root",
    roles = "role/swarming.taskTriggerer",
    groups = "project-art-admins",
)

# Resources shared by all subprojects.

luci.realm(name = "pools/ci")
luci.bucket(name = "ci")

# Shadow bucket is needed for LED.
luci.bucket(
    name = "ci.shadow",
    shadows = "ci",
    bindings = [
        luci.binding(
            roles = "role/buildbucket.creator",
            users = ["art-ci-builder@chops-service-accounts.iam.gserviceaccount.com"],
        ),
        luci.binding(
            roles = "role/buildbucket.triggerer",
            users = ["art-ci-builder@chops-service-accounts.iam.gserviceaccount.com"],
        ),
    ],
    constraints = luci.bucket_constraints(
        pools = ["luci.art.ci"],
        service_accounts = ["art-ci-builder@chops-service-accounts.iam.gserviceaccount.com"],
    ),
    dynamic = True,
)

luci.notifier_template(
    name = "default",
    body = io.read_file("luci-notify.template"),
)

luci.console_view(
    name = "luci",
    repo = "https://android.googlesource.com/platform/art",
    title = "ART LUCI Console",
    refs = ["refs/heads/master"],
    include_experimental_builds = True,
)

luci.notifier(
    name = "art-team+chromium-buildbot",
    on_new_status = [
        "FAILURE",
        "INFRA_FAILURE",
    ],
    notify_emails = [
        "art-team+chromium-buildbot@google.com",
    ],
)

luci.gitiles_poller(
    name = "art",
    bucket = "ci",
    repo = "https://android.googlesource.com/platform/art",
    refs = ["refs/heads/master"],
)

luci.gitiles_poller(
    name = "libcore",
    bucket = "ci",
    repo = "https://android.googlesource.com/platform/libcore",
    refs = ["refs/heads/master"],
)

luci.gitiles_poller(
    name = "vogar",
    bucket = "ci",
    repo = "https://android.googlesource.com/platform/external/vogar",
    refs = ["refs/heads/master"],
)

luci.gitiles_poller(
    name = "manifest",
    bucket = "ci",
    repo = "https://android.googlesource.com/platform/manifest",
    refs = ["refs/heads/master-art"],
)

def ci_builder(name, category, short_name, dimensions, properties={},
               experiments={}, hidden=False):
    luci.builder(
        name = name,
        bucket = "ci",
        executable = luci.recipe(
            cipd_package = "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build",
            cipd_version = "refs/heads/main",
            name = "art",
        ),
        dimensions = dimensions | {
            "pool": "luci.art.ci",
        },
        service_account = "art-ci-builder@chops-service-accounts.iam.gserviceaccount.com",

        # Maximum delay between scheduling a build and the build actually starting.
        # In a healthy state (enough free/idle devices), the delay is fairly small,
        # but if enough devices are offline, this timeout will cause INFRA_FAILURE.
        # Set the value reasonably high to prefer delayed builds over failing ones.
        # NB: LUCI also enforces (expiration_timeout + execution_timeout <= 47).
        expiration_timeout = 17 * time.hour,
        execution_timeout = 30 * time.hour,
        build_numbers = True,
        properties = properties,
        caches = [
            # Directory called "art" that persists from build to build (one per bot).
            # We can checkout and build in this directory to get fast incremental builds.
            swarming.cache("art", name = "art"),
        ],
        notifies = ["art-team+chromium-buildbot"] if not hidden else [],
        triggered_by = [
            "art",
            "libcore",
            "manifest",
            "vogar",
        ],
        experiments = experiments,
    )
    if not hidden:
        luci.console_view_entry(
            console_view = "luci",
            builder = name,
            category = category,
            short_name = short_name,
        )

def add_builder(mode,
                arch,
                bitness,
                ndebug=False,
                ngen=False,
                cmc=False,
                gcstress=False,
                poison=False,
                hidden=False):
    def check_arg(value, valid_values):
      if value not in valid_values:
        fail("Argument '{}' was expected to be on of {}".format(value, valid_values))
    check_arg(mode, ["target", "host", "qemu"])
    check_arg(arch, ["arm", "x86", "riscv"])
    check_arg(bitness, [32, 64])

    # Create builder name based on the configuaration parameters.
    name = mode + '.' + arch
    name += '.gcstress' if gcstress else ''
    name += '.poison' if poison else ''
    name += '.ngen' if ngen else ''
    name += '.cmc' if cmc else ''
    name += '.ndebug' if ndebug else ''
    name += '.' + str(bitness)
    name = name.replace("ngen.cmc", "ngen-cmc")

    # Define the LUCI console category (the tree layout).
    # The "|" splits the tree node into sub-categories.
    # Merge some name parts to reduce the tree depth.
    category = name.replace(".", "|")
    category = category.replace("host|", "host.")
    category = category.replace("target|", "target.")
    category = category.replace("gcstress|cmc", "gcstress-cmc")

    product = None
    if arch == "arm":
      product = "armv8" if bitness == 64 else "arm_krait"
    if arch == "riscv":
      product = "riscv64"

    dimensions = {"os": "Android" if mode == "target" else "Ubuntu"}
    if mode == "target":
      if cmc:
        # Request devices running at least Android 24Q3 (`AP1A` builds) for
        # (`userfaultfd`-based) Concurrent Mark-Compact GC configurations.
        # Currently (as of 2024-08-22), the only devices within the device pool
        # allocated to ART that are running `AP1A` builds are Pixel 6 devices
        # (all other device types are running older Android versions), which are
        # also the only device model supporting `userfaultfd` among that pool.
        dimensions |= {"device_os": "A|B"}
      else:
        # Run all other configurations on Android S since it is the oldest we support.
        # Other than the `AP1A` builds above, all other devices are flashed to `SP2A`.
        # This avoids allocating `userfaultfd` devices for tests that don't need it.
        dimensions |= {"device_os": "S"}
    elif mode == "host":
      dimensions |= {"cores": "8"}
    elif mode == "qemu":
      dimensions |= {"cores": "16"}

    testrunner_args = ['--verbose', '--host'] if mode == 'host' else ['--target', '--verbose']
    testrunner_args += ['--ndebug'] if ndebug else ['--debug']
    testrunner_args += ['--gcstress'] if gcstress else []

    properties = {
        "builder_group": "client.art",
        "bitness": bitness,
        "build_only": ("build_only" in name),
        "debug": not ndebug,
        "device": None if mode == "host" else "-".join(name.split("-")[:2]),
        "on_virtual_machine": mode == "qemu",
        "product": product,
        "concurrent_collector": not cmc,
        "generational_cc": not ngen,
        "gcstress": gcstress,
        "heap_poisoning": poison,
        "testrunner_args": testrunner_args,
    }

    experiments = {"art.superproject": 100} if mode == "qemu" else {}

    ci_builder(name=name,
               category="|".join(category.split("|")[:-1]),
               short_name=category.split("|")[-1],
               dimensions=dimensions,
               properties={k:v for k, v in properties.items() if v},
               experiments=experiments,
               hidden=hidden)

def add_builders():
  for mode, arch in [("target", "arm"), ("host", "x86")]:
    for bitness in [32, 64]:
      # Add first to keep these builders together and left-aligned in the console.
      add_builder(mode, arch, bitness)
    for bitness in [32, 64]:
      add_builder(mode, arch, bitness, ndebug=True)
      if mode == "host":
        add_builder(mode, arch, bitness, ngen=True, cmc=True)
      add_builder(mode, arch, bitness, cmc=True)
      add_builder(mode, arch, bitness, poison=True)
      add_builder(mode, arch, bitness, gcstress=True)
      add_builder(mode, arch, bitness, cmc=True, gcstress=True)
  add_builder('qemu', 'arm', bitness=64)
  add_builder('qemu', 'riscv', bitness=64)

add_builders()