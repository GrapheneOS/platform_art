/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import pkg1.A;
import pkg1.C;
import pkg1.C2;
import pkg2.B;
import pkg2.D;
import pkg2.D2;

public class Main {
    public static void main(String args[]) {
        // A single method signature can result in multiple vtable entries
        // when package-private methods from different packages are involved.
        // All classes here define the method `void foo()` but classes
        //     class pkg1.A { ... }
        //     class pkg2.B extends pkg1.A { ... }
        //     class pkg1.C extends pkg2.B { ... }
        //     class pkg2.D extends pkg1.C { ... }
        // define it as package-private and classes
        //     class pkg1.C2 extends pkg2.B { ... }
        //     class pkg2.D2 extends pkg1.C2 { ... }
        // define it as public, so that we can test different cases of overriding.

        A a = new A();
        a.callAFoo();  // pkg1.A.foo

        B b = new B();
        b.callAFoo();  // pkg1.A.foo (not overridden by pkg2.B.foo)
        b.callBFoo();  // pkg2.B.foo

        C c = new C();
        c.callAFoo();  // pkg1.C.foo (overriddes pkg1.A.foo)
        c.callBFoo();  // pkg2.B.foo (not overridden by pkg1.C.foo)
        c.callCFoo();  // pkg1.C.foo

        D d = new D();
        d.callAFoo();  // pkg1.C.foo (not overridden by pkg2.D.foo)
        d.callBFoo();  // pkg2.D.foo (overrides pkg2.B.foo)
        d.callCFoo();  // pkg1.C.foo (not overridden by pkg2.D.foo)
        d.callDFoo();  // pkg2.D.foo

        C2 c2 = new C2();
        c2.callAFoo();  // pkg1.C2.foo (overriddes pkg1.A.foo)
        c2.callBFoo();  // pkg2.B.foo (not overridden by pkg1.C2.foo)
        c2.callC2Foo();  // pkg1.C2.foo

        D2 d2 = new D2();
        d2.callAFoo();  // pkg2.D2.foo (overrides public pkg2.C2.foo which overrides pkg1.A.foo)
        d2.callBFoo();  // pkg2.D2.foo (overrides package-private pkg2.B.foo in the same package)
        d2.callC2Foo();  // pkg2.D2.foo (overrides public pkg2.C2.foo)
        d2.callD2Foo();  // pkg2.D2.foo
    }
}
