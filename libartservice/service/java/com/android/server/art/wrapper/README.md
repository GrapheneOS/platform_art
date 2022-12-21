This folder contains temporary wrappers that access system server internal
classes using reflection. Having the wrappers is the workaround for the current
time being where required system APIs are not finalized. The classes and methods
correspond to system APIs planned to be exposed.

The mappings are:
- `PackageUserState`: `com.android.server.pm.pkg.PackageUserState`
