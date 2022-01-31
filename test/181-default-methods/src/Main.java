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

public class Main {
    private static boolean usingDalvik = "Dalvik".equals(System.getProperty("java.vm.name"));

    public static void expectSameString(String expected, String actual) {
        if (expected != actual) {
            throw new Error("Expected " + expected + ", got " + actual + " (different object)");
        }
    }

    public static void expectDefault(Super target) {
        String output = target.testMethod();  // invoke-virtual Super.testMethod()
        Abstract abstractTarget = target;
        String output2 = abstractTarget.testMethod();  // invoke-interface Abstract.testMethod()
        expectSameString(output, output2);
        System.out.println("Output from " + target.getClass().getName() + ": " + output);
    }

    public static void expectConflict(Super target) {
        try {
            String output = target.testMethod();  // invoke-virtual Super.testMethod()
            throw new Error("Unexpected success for " + target.getClass().getName() +
                            " output: " + output);
        } catch (AbstractMethodError ame) {
            if (usingDalvik) {
                throw new Error("Unexpected AbstractMethodError", ame);
            }  // else the AME is expected on RI.
        } catch (IncompatibleClassChangeError expected) {
        }
        try {
            Abstract abstractTarget = target;
            String output = abstractTarget.testMethod();  // invoke-interface Abstract.testMethod()
            throw new Error("Unexpected success for " + target.getClass().getName() +
                            " output: " + output);
        } catch (AbstractMethodError ame) {
            if (usingDalvik) {
                throw new Error("Unexpected AbstractMethodError", ame);
            }  // else the AME is expected on RI.
        } catch (IncompatibleClassChangeError expected) {
        }
        System.out.println("Conflict in class " + target.getClass().getName());
    }

    public static void expectMiranda(Super target) {
        try {
            String output = target.testMethod();  // invoke-virtual Super.testMethod()
            throw new Error("Unexpected success for " + target.getClass().getName() +
                            " output: " + output);
        } catch (AbstractMethodError expected) {
        }
        try {
            Abstract abstractTarget = target;
            String output = abstractTarget.testMethod();  // invoke-interface Abstract.testMethod()
            throw new Error("Unexpected success for " + target.getClass().getName() +
                            " output: " + output);
        } catch (AbstractMethodError expected) {
        }
        System.out.println("Miranda in class " + target.getClass().getName());
    }

    public static void main(String args[]) {
        // Basic tests that have the interfaces D<n> with default method and/or
        // D<n>M interfaces that mask the default method ordered by <n>.
        expectMiranda(new Super());
        expectDefault(new Default_D1());
        expectDefault(new Default_D2());
        expectDefault(new Default_D3());
        expectMiranda(new Miranda_D1M());
        expectMiranda(new Miranda_D2M());
        expectMiranda(new Miranda_D3M());
        expectConflict(new Conflict_D1_D2());
        expectDefault(new Default_D1_D2M());
        expectDefault(new Default_D1M_D2());
        expectMiranda(new Miranda_D1M_D2M());
        expectConflict(new Conflict_D1_D2_D3());
        expectConflict(new Conflict_D1_D2_D3M());
        expectConflict(new Conflict_D1_D2M_D3());
        expectDefault(new Default_D1_D2M_D3M());
        expectConflict(new Conflict_D1M_D2_D3());
        expectDefault(new Default_D1M_D2_D3M());
        expectDefault(new Default_D1M_D2M_D3());
        expectMiranda(new Miranda_D1M_D2M_D3M());

        // Cases where one interface masks the method in more than one superinterface.
        expectMiranda(new Miranda_D1D2M());
        expectDefault(new Default_D1D2D());
        expectMiranda(new Miranda_AD1D2M());
        expectDefault(new Default_AD1D2D());

        // Cases where the interface D2 is early in the interface table but masked by D2M later.
        expectDefault(new Default_D2_D1_D2M());
        expectMiranda(new Miranda_D2_D1M_D2M());

        // Cases that involve a superclass with miranda method in the vtable.
        // Note: The above cases also include a miranda method in the superclass vtable
        // anyway because we want to test `invoke-virtual Super.testMethod()` as well
        // as the `invoke-interface Abstract.testMethod()`. However, miranda methods in
        // superclass vtable mean that all default methods in superclass interfaces,
        // if any, have been masked by abstract method, so processing them is a no-op.
        expectDefault(new Default_D1M_x_D2());
        expectMiranda(new Miranda_D1M_x_D2M());
        expectConflict(new Conflict_D1M_x_D2_D3());
        expectDefault(new Default_D1M_x_D2_D3M());
        expectDefault(new Default_D1M_x_D2M_D3());
        expectMiranda(new Miranda_D1M_x_D2M_D3M());

        // Cases that involve a superclass with default method in the vtable.
        expectConflict(new Conflict_D1_x_D2());
        expectDefault(new Default_D1_x_D2M());
        expectDefault(new Default_D1_x_D1MD());
        expectMiranda(new Miranda_D1_x_D1M());
        expectConflict(new Conflict_D1_x_D2_D3());
        expectConflict(new Conflict_D1_x_D2_D3M());
        expectConflict(new Conflict_D1_x_D2M_D3());
        expectDefault(new Default_D1_x_D2M_D3M());
        expectConflict(new Conflict_D1_x_D1MD_D2());
        expectDefault(new Default_D1_x_D1MD_D2M());
        expectDefault(new Default_D1_x_D1M_D2());
        expectMiranda(new Miranda_D1_x_D1M_D2M());

        // Cases that involve a superclass with conflict method in the vtable.
        expectDefault(new Default_D1_D2_x_D2M());
        expectDefault(new Default_D1_D2_x_D1M());
        expectMiranda(new Miranda_D1_D2_x_D1M_D2M());
        expectDefault(new Default_D1_D2_x_D1MD_D2M());
        expectConflict(new Conflict_D1_D2_x_D1M_D3());
        expectDefault(new Default_D1_D2_x_D1M_D3M());
        expectConflict(new Conflict_D1_D2_x_D2M_D3());
        expectDefault(new Default_D1_D2_x_D2M_D3M());
        expectConflict(new Conflict_D1_D2_x_D1MD_D3());
        expectConflict(new Conflict_D1_D2_x_D1MD_D3M());
        expectDefault(new Default_D1_D2_D3M_x_D1MD_D2M());
        expectMiranda(new Miranda_D1_D2_D3M_x_D1M_D2M());
        expectDefault(new Default_D1_D2_x_D1D2D());
        expectMiranda(new Miranda_D1_D2_x_D1D2M());
        expectDefault(new Default_D1_D2_x_AD1D2D());
        expectMiranda(new Miranda_D1_D2_x_AD1D2M());
        expectConflict(new Conflict_D1_D2_D3_x_D1D2D());
        expectDefault(new Default_D1_D2_D3_x_D1D2M());
        expectConflict(new Conflict_D1_D2_D3_x_AD1D2D());
        expectDefault(new Default_D1_D2_D3_x_AD1D2M());
        expectDefault(new Default_D1_D2_D3M_x_D1D2D());
        expectMiranda(new Miranda_D1_D2_D3M_x_D1D2M());
        expectDefault(new Default_D1_D2_D3M_x_AD1D2D());
        expectMiranda(new Miranda_D1_D2_D3M_x_AD1D2M());
    }
}

class Default_D1 extends Super implements Abstract, D1 {}
class Default_D2 extends Super implements Abstract, D2 {}
class Default_D3 extends Super implements Abstract, D3 {}
class Miranda_D1M extends Super implements Abstract, D1M {}
class Miranda_D2M extends Super implements Abstract, D2M {}
class Miranda_D3M extends Super implements Abstract, D3M {}
class Conflict_D1_D2 extends Super implements Abstract, D1, D2 {}
class Default_D1_D2M extends Super implements Abstract, D1, D2M {}
class Default_D1M_D2 extends Super implements Abstract, D1M, D2 {}
class Miranda_D1M_D2M extends Super implements Abstract, D1M, D2M {}
class Conflict_D1_D2_D3 extends Super implements Abstract, D1, D2, D3 {}
class Conflict_D1_D2_D3M extends Super implements Abstract, D1, D2, D3M {}
class Conflict_D1_D2M_D3 extends Super implements Abstract, D1, D2M, D3 {}
class Default_D1_D2M_D3M extends Super implements Abstract, D1, D2M, D3M {}
class Conflict_D1M_D2_D3 extends Super implements Abstract, D1M, D2, D3 {}
class Default_D1M_D2_D3M extends Super implements Abstract, D1M, D2, D3M {}
class Default_D1M_D2M_D3 extends Super implements Abstract, D1M, D2M, D3 {}
class Miranda_D1M_D2M_D3M extends Super implements Abstract, D1M, D2M, D3M {}

class Miranda_D1D2M extends Super implements D1D2M {}
class Default_D1D2D extends Super implements D1D2D {}
class Miranda_AD1D2M extends Super implements AD1D2M {}
class Default_AD1D2D extends Super implements AD1D2D {}

class Default_D2_D1_D2M extends Super implements Abstract, D1, D2M {}
class Miranda_D2_D1M_D2M extends Super implements Abstract, D1M, D2M {}

class Default_D1M_x_D2 extends Miranda_D1M implements D2 {}
class Miranda_D1M_x_D2M extends Miranda_D1M implements D2M {}
class Conflict_D1M_x_D2_D3 extends Miranda_D1M implements D2, D3 {}
class Default_D1M_x_D2_D3M extends Miranda_D1M implements D2, D3M {}
class Default_D1M_x_D2M_D3 extends Miranda_D1M implements D2M, D3 {}
class Miranda_D1M_x_D2M_D3M extends Miranda_D1M implements D2M, D3M {}

class Conflict_D1_x_D2 extends Default_D1 implements D2 {}
class Default_D1_x_D2M extends Default_D1 implements D2M {}
class Default_D1_x_D1MD extends Default_D1 implements D1MD {}
class Miranda_D1_x_D1M extends Default_D1 implements D1M {}
class Conflict_D1_x_D2_D3 extends Default_D1 implements D2, D3 {}
class Conflict_D1_x_D2_D3M extends Default_D1 implements D2, D3M {}
class Conflict_D1_x_D2M_D3 extends Default_D1 implements D2M, D3 {}
class Default_D1_x_D2M_D3M extends Default_D1 implements D2M, D3M {}
class Conflict_D1_x_D1MD_D2 extends Default_D1 implements D1MD, D2 {}
class Default_D1_x_D1MD_D2M extends Default_D1 implements D1MD, D2M {}
class Default_D1_x_D1M_D2 extends Default_D1 implements D1M, D2 {}
class Miranda_D1_x_D1M_D2M extends Default_D1 implements D1M, D2M {}

class Default_D1_D2_x_D2M extends Conflict_D1_D2 implements D2M {}
class Default_D1_D2_x_D1M extends Conflict_D1_D2 implements D1M {}
class Miranda_D1_D2_x_D1M_D2M extends Conflict_D1_D2 implements D1M, D2M {}
class Default_D1_D2_x_D1MD_D2M extends Conflict_D1_D2 implements D1MD, D2M {}
class Conflict_D1_D2_x_D1M_D3 extends Conflict_D1_D2 implements D1M, D3 {}
class Default_D1_D2_x_D1M_D3M extends Conflict_D1_D2 implements D1M, D3M {}
class Conflict_D1_D2_x_D2M_D3 extends Conflict_D1_D2 implements D2M, D3 {}
class Default_D1_D2_x_D2M_D3M extends Conflict_D1_D2 implements D2M, D3M {}
class Conflict_D1_D2_x_D1MD_D3 extends Conflict_D1_D2 implements D1MD, D3 {}
class Conflict_D1_D2_x_D1MD_D3M extends Conflict_D1_D2 implements D1MD, D3M {}
class Default_D1_D2_D3M_x_D1MD_D2M extends Conflict_D1_D2_D3M implements D1MD, D2M {}
class Miranda_D1_D2_D3M_x_D1M_D2M extends Conflict_D1_D2_D3M implements D1M, D2M {}
class Miranda_D1_D2_x_D1D2M extends Conflict_D1_D2 implements D1D2M {}
class Default_D1_D2_x_D1D2D extends Conflict_D1_D2 implements D1D2D {}
class Miranda_D1_D2_x_AD1D2M extends Conflict_D1_D2 implements AD1D2M {}
class Default_D1_D2_x_AD1D2D extends Conflict_D1_D2 implements AD1D2D {}
class Default_D1_D2_D3_x_D1D2M extends Conflict_D1_D2_D3 implements D1D2M {}
class Conflict_D1_D2_D3_x_D1D2D extends Conflict_D1_D2_D3 implements D1D2D {}
class Default_D1_D2_D3_x_AD1D2M extends Conflict_D1_D2_D3 implements AD1D2M {}
class Conflict_D1_D2_D3_x_AD1D2D extends Conflict_D1_D2_D3 implements AD1D2D {}
class Miranda_D1_D2_D3M_x_D1D2M extends Conflict_D1_D2_D3M implements D1D2M {}
class Default_D1_D2_D3M_x_D1D2D extends Conflict_D1_D2_D3M implements D1D2D {}
class Miranda_D1_D2_D3M_x_AD1D2M extends Conflict_D1_D2_D3M implements AD1D2M {}
class Default_D1_D2_D3M_x_AD1D2D extends Conflict_D1_D2_D3M implements AD1D2D {}

