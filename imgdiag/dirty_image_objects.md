# How to update dirty-image-objects

1. Add `imgdiag` to ART APEX.

The easiest way is to modify `art/build/apex/Android.bp` like this:
```
 art_runtime_binaries_both = [
     "dalvikvm",
     "dex2oat",
+    "imgdiag",
 ]
```

2. Install ART APEX and reboot, e.g.:

```
m apps_only dist
adb install out/dist/com.android.art.apex
adb reboot
```

3. Collect imgdiag output.

```
# To see all options check: art/imgdiag/run_imgdiag.py -h

art/imgdiag/run_imgdiag.py
```

4. Create new dirty-image-objects.

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

5. Push new dirty-image-objects to the device.

```
adb push dirty-image-objects.txt /etc/dirty-image-objects
```

6. Reinstall ART APEX to update the boot image.

```
adb install out/dist/com.android.art.apex
adb reboot
```

At this point the device should have new `boot.art` with optimized dirty object layout.
This can be checked by collecting imgdiag output again and comparing dirty page counts to the previous run.
