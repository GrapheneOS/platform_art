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
        # Publicly readable.
        acl.entry(
            roles = [
                acl.BUILDBUCKET_READER,
                acl.LOGDOG_READER,
                acl.PROJECT_CONFIGS_READER,
                acl.SCHEDULER_READER,
            ],
            groups = "all",
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
            groups = "all",
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

def ci_builder(name, category, short_name, dimensions):
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
        properties = {
            "builder_group": "client.art",
        },
        caches = [
            # Directory called "art" that persists from build to build (one per bot).
            # We can checkout and build in this directory to get fast incremental builds.
            swarming.cache("art", name = "art"),
        ],
        notifies = ["art-team+chromium-buildbot"],
        triggered_by = [
            "art",
            "libcore",
            "manifest",
            "vogar",
        ],
    )
    luci.console_view_entry(
        console_view = "luci",
        builder = name,
        category = category,
        short_name = short_name,
    )

# Dimensions specify which bots we can run on.
host_dims = {"os": "Linux"}
target_dims = {"os": "Android"}
arm_target_dims = target_dims | {"device_type": "bonito|oriole|walleye"}
x86_target_dims = target_dims | {"device_type": "fugu"}

# userfault-GC configurations must be run on Pixel 6.
userfault_gc_target_dims = target_dims | {"device_type": "oriole"}

ci_builder("angler-armv7-debug", "angler|armv7", "dbg", arm_target_dims)
ci_builder("angler-armv7-non-gen-cc", "angler|armv7", "ngen", userfault_gc_target_dims)
ci_builder("angler-armv7-ndebug", "angler|armv7", "ndbg", arm_target_dims)
ci_builder("angler-armv8-debug", "angler|armv8", "dbg", arm_target_dims)
ci_builder("angler-armv8-non-gen-cc", "angler|armv8", "ngen", userfault_gc_target_dims)
ci_builder("angler-armv8-ndebug", "angler|armv8", "ndbg", arm_target_dims)
ci_builder("bullhead-armv7-gcstress-ndebug", "bullhead|armv7|gcstress", "dbg", arm_target_dims)
ci_builder("bullhead-armv8-gcstress-debug", "bullhead|armv8|gcstress", "dbg", arm_target_dims)
ci_builder("bullhead-armv8-gcstress-ndebug", "bullhead|armv8|gcstress", "ndbg", arm_target_dims)
ci_builder("fugu-debug", "fugu", "dbg", x86_target_dims)
ci_builder("fugu-ndebug", "fugu", "ndbg", x86_target_dims)
ci_builder("host-x86-cms", "host|x86", "cms", host_dims)
ci_builder("host-x86-debug", "host|x86", "dbg", host_dims)
ci_builder("host-x86-ndebug", "host|x86", "ndbg", host_dims)
ci_builder("host-x86-gcstress-debug", "host|x86", "gcs", host_dims)
ci_builder("host-x86-poison-debug", "host|x86", "psn", host_dims)
ci_builder("host-x86_64-cdex-fast", "host|x64", "cdx", host_dims)
ci_builder("host-x86_64-cms", "host|x64", "cms", host_dims)
ci_builder("host-x86_64-debug", "host|x64", "dbg", host_dims)
ci_builder("host-x86_64-non-gen-cc", "host|x64", "ngen", host_dims)
ci_builder("host-x86_64-ndebug", "host|x64", "ndbg", host_dims)
ci_builder("host-x86_64-poison-debug", "host|x64", "psn", host_dims)
ci_builder("walleye-armv7-poison-debug", "walleye|armv7|poison", "dbg", arm_target_dims)
ci_builder("walleye-armv8-poison-debug", "walleye|armv8|poison", "dbg", arm_target_dims)
ci_builder("walleye-armv8-poison-ndebug", "walleye|armv8|poison", "ndbg", arm_target_dims)
