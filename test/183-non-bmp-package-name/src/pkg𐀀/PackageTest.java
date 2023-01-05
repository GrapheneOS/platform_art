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

package pkg𐀀;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public class PackageTest {
    public static void main() {
        testJniFindClass();
        testClassForName();
        testProxy();
    }

    private static void testJniFindClass() {
        try {
            // Cannot import "Main". Use reflection to call `Main.jniFindClass()`.
            Class<?> mainClass = Class.forName("Main");
            Method jniFindClass = mainClass.getMethod("jniFindClass", String.class);
            Class<?> klass = (Class<?>) jniFindClass.invoke(null, "pkg𐀀/PackageTest");
            if (klass != PackageTest.class) {
              System.out.println("Unexpected class: " + klass);
            }
        } catch (InvocationTargetException ite) {
            ite.getCause().printStackTrace(System.out);  // Unwrap the cause.
        } catch (Throwable t) {
            t.printStackTrace(System.out);
        }
    }

    private static void testClassForName() {
        try {
            Class<?> klass = Class.forName("pkg𐀀.PackageTest");
            if (klass != PackageTest.class) {
              System.out.println("Unexpected class: " + klass);
            }
        } catch (Throwable t) {
            t.printStackTrace(System.out);
        }
    }

    private static void testProxy() {
        try {
            InvocationHandler handler = new PackageInvocationHandler();
            Class<?> proxyClass = Proxy.getProxyClass(
                    PackageTestInterface.class.getClassLoader(), PackageTestInterface.class);
            Constructor<?> ctor = proxyClass.getConstructor(InvocationHandler.class);
            Object proxy = ctor.newInstance(handler);
            PackageTestInterface asInterface = (PackageTestInterface) proxy;
            asInterface.interfaceMethod();
        } catch (Throwable t) {
            t.printStackTrace(System.out);
        }
    }
}

interface PackageTestInterface {
    public void interfaceMethod();
}

class PackageInvocationHandler implements InvocationHandler {
    public Object invoke(Object proxy, Method method, Object[] args) {
        System.out.println("Invoke " + method);
        return null;
    }
}
