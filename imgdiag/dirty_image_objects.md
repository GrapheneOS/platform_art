# How to update dirty-image-objects (from ATP top 100 run)

## 1. Collect dirty objects from most used apps using ATP top100 test run (use specific branch/Android release)

**IMPORTANT**: Make sure that pinned build for `GIT_TRUNK_RELEASE` branch is up-to-date. Update in `BUILD_IDS` here:
<https://source.corp.google.com/piper///depot/google3/configs/wireless/android/testing/atp/prod/mainline-engprod/common.gcl;l=28-44>


**NOTE**: New branches can be added in `master-art_imgdiag_tests` definition here:
<https://source.corp.google.com/piper///depot/google3/configs/wireless/android/testing/atp/prod/mainline-engprod/config.gcl;l=1384-1395>


Go to <http://go/abtd> and create a test run with the following parameters:

* Branch name: `git_main`

* Test type: `ATP`

* Test name: select one of the supported imgdiag ATP configurations, see the list here:
<https://atp.googleplex.com/test_configs?testName=%25imgdiag_top_100%25>


This will generate a profile based on the following:

* ART module – latest version from `git_main`.

* Top 100 apps – up-to-date list of app versions.

* Platform – pinned version from <https://source.corp.google.com/piper///depot/google3/configs/wireless/android/testing/atp/prod/mainline-engprod/common.gcl;l=28-44>


## 2. Create two CLs with new dirty-image-objects files (one for ART, one for frameworks)


Download new dirty-image-objects files from the run, either manually from the
website or using the script. Example:
```
ati_download_artifacts.py --invocation-id I70700010348949160
```


**NOTE**: Use `ati_download_artifacts.py -h` to see all command line flags.


There should be two files named like this:
```
subprocess-dirty-image-objects-art_9607835265532903390.txt.gz
subprocess-dirty-image-objects-framework_10599183892981195850.txt.gz
```

dirty-image-objects-art goes into platform/art repo: <https://cs.android.com/android/platform/superproject/main/+/main:art/build/apex/dirty-image-objects>


dirty-image-objects-framework goes into platform/frameworks/base repo: <https://cs.android.com/android/platform/superproject/main/+/main:frameworks/base/config/dirty-image-objects>


Upload the two CLs and put them in the same topic.

## 3. Start an ABTD build from the same branch with dirty-image-objects CLs on top

Create a build for the same branch and target that was used in step 1, apply CLs with new dirty-image-objects.
For `v2/mainline-engprod/apct/mainline/art/GIT_TRUNK_RELEASE/panther/imgdiag_top_100`:

* Branch: `git_trunk-release`

* Target: `panther-userdebug`

## 4. Run ATP test again, this time with custom build-id from step 3 and with CLs from step 2

Rerun the test from step 1 with the following additions:
* Add the CLs with new dirty-image-objects (this will force ABTD to rebuild ART module with the change)
* Select "Configure this test in **Advanced mode**". Scroll down to "**Extra Arguments**" and add **build-id**, set it to the build id from step 3.

## 5. Download ATP results from step 1 and step 4 and compare them

Use the script to download both ATP run results, example:
```
ati_download_artifacts.py --invocation-id I79100010355578895 --invocation-id I84900010357346481
```

Compare the results:
```
~/compare_imgdiag_runs.py I79100010355578895 I84900010357346481

# Example comparison output:

Comparing:  ['I79100010355578895', 'I84900010357346481']
Common proc count (used in the comparison):  233
Mismatching proc count (not present in all runs):  10
Total dirty pages:
[17066, 14799]

Mean dirty pages:
[73.24463519313305, 63.51502145922747]

Median dirty pages:
[69, 60]
```

Note the number of common processes, it should be at least 200 (approx 100 from top100 apps + another 100+ system processes). If it is lower than that, it might indicate missing processes in one or both runs, which would invalidate the comparison.
The lower the number of mismatching processes the better. Typically less than 40 is fine.


In there is a significant difference between the mean and median dirty page counts, it may be useful to check page count diffs for specific processess. Use `--verbose` flag with comparison script to show such info.

The key number to look at for comparison is "mean dirty pages". In this example new profile saves about 10 pages per process (a mean reduction of 40 KiB memory per process).

## 6. Submit new dirty-image-objects CLs if the result is better/good enough

Typical measurement noise for mean dirty pages is less than 2. A new profile with dirty page reduction greater than this threshold is considered an improvement.


# How to update dirty-image-objects (manually)

## 1. Install imgdiag ART APEX on a device with AOSP build, e.g.:

```
. ./build/envsetup.sh
banchan test_imgdiag_com.android.art module_arm64
m apps_only dist
adb install out/dist/test_imgdiag_com.android.art.apex
adb reboot
```

## 2. Collect imgdiag output.

```
# To see all options check: art/imgdiag/run_imgdiag.py -h

art/imgdiag/run_imgdiag.py
```

## 3. Create new dirty-image-objects files.

```
# To see all options check: art/imgdiag/create_dirty_image_objects.py -h

# Using all imgdiag files:
art/imgdiag/create_dirty_image_objects.py ./imgdiag_*

# Or using only specified files:
art/imgdiag/create_dirty_image_objects.py \
  ./imgdiag_system_server.txt \
  ./imgdiag_com.android.systemui.txt \
  ./imgdiag_com.google.android.gms.txt \
  ./imgdiag_com.google.android.gms.persistent.txt \
  ./imgdiag_com.google.android.gms.ui.txt \
  ./imgdiag_com.google.android.gms.unstable.txt
```

This will generate 3 files:
* dirty-image-objects.txt -- contains all dirty objects + info about the jar they are defined in.
* art\_dirty-image-objects.txt -- dirty objects defined in the ART module, these objects are defined in art/build/apex/dirty-image-objects
* framework\_dirty\_image\_objects.txt -- dirty objects defined in frameworks, these objects are defined in frameworks/base/config/dirty-image-objects


The resulting art/framework files will contain lists of dirty objects with
optional (enabled by default) sort keys in the following format:
```
<class_descriptor>[.<reference_field_name>:<reference_field_type>]* [<sort_key>]
```
Classes are specified using a descriptor and objects are specified by
a reference chain starting from a class. Example:
```
# Mark FileUtils class as dirty:
Landroid/os/FileUtils; 4
# Mark instance of Property class as dirty:
Landroid/view/View;.SCALE_X:Landroid/util/Property; 4
```
If present, sort keys are used to specify the ordering between dirty entries.
All dirty objects will be placed in the dirty bin of the boot image and sorted
by the sort\_key values. I.e., dirty entries with sort\_key==N will have lower
address than entries with sort\_key==N+1.

## 4. Push new frameworks dirty-image-objects to the device.

```
adb push framework_dirty-image-objects.txt /etc/dirty-image-objects
```

## 5. Install ART module with new dirty-image-objects

```
cp ./art_dirty-image-objects.txt $ANDROID_BUILD_TOP/art/build/apex/dirty-image-objects
m apps_only dist
adb install out/dist/com.android.art.apex
adb reboot
```

At this point the device should have new `boot.art` with optimized dirty object layout.
This can be checked by collecting imgdiag output again and comparing dirty page counts to the previous run.
