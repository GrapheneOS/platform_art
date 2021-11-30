/*
 * Copyright (C) 2017 The Android Open Source Project
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

import static art.Redefinition.doCommonClassRedefinition;
import java.util.Base64;
import java.util.Random;
import java.util.function.*;
import java.util.stream.*;

public class Main {

  // The bytes below define the following java program.
  // package java.lang;
  // import java.math.*;
  // public final class Long extends Number implements Comparable<Long> {
  //     public static final long MIN_VALUE = 0;
  //     public static final long MAX_VALUE = 0;
  //     public static final Class<Long> TYPE = null;
  //     static { }
  //     // Used for Stream.count for some reason.
  //     public static long sum(long a, long b) {
  //       return a + b;
  //     }
  //     // Used in stream/lambda functions.
  //     public Long(long value) {
  //       this.value = value;
  //     }
  //     // Used in stream/lambda functions.
  //     public static Long valueOf(long l) {
  //       return new Long(l);
  //     }
  //     // Intrinsic! Do something cool. Return i + 1
  //     public static long highestOneBit(long i) {
  //       return i + 1;
  //     }
  //     // Intrinsic! Do something cool. Return i - 1
  //     public static long lowestOneBit(long i) {
  //       return i - 1;
  //     }
  //     // Intrinsic! Do something cool. Return i + i
  //     public static int numberOfLeadingZeros(long i) {
  //       return (int)(i + i);
  //     }
  //     // Intrinsic! Do something cool. Return i & (i >>> 1);
  //     public static int numberOfTrailingZeros(long i) {
  //       return (int)(i & (i >>> 1));
  //     }
  //     // Intrinsic! Do something cool. Return 5
  //      public static int bitCount(long i) {
  //        return 5;
  //      }
  //     // Intrinsic! Do something cool. Return i
  //     public static long rotateLeft(long i, int distance) {
  //       return i;
  //     }
  //     // Intrinsic! Do something cool. Return 10 * i
  //     public static long rotateRight(long i, int distance) {
  //       return 10 * i;
  //     }
  //     // Intrinsic! Do something cool. Return -i
  //     public static long reverse(long i) {
  //       return -i;
  //     }
  //     // Intrinsic! Do something cool. Return 0
  //     public static int signum(long i) {
  //       return 0;
  //     }
  //     // Intrinsic! Do something cool. Return 0
  //     public static long reverseBytes(long i) {
  //       return 0;
  //     }
  //     public String toString() {
  //       return "Redefined Long! value (as double)=" + ((double)value);
  //     }
  //     public static String toString(long i, int radix) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toUnsignedString(long i, int radix) {
  //       throw new Error("Method redefined away!");
  //     }
  //     private static BigInteger toUnsignedBigInteger(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toHexString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toOctalString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toBinaryString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     static String toUnsignedString0(long val, int shift) {
  //       throw new Error("Method redefined away!");
  //     }
  //     static void formatUnsignedLong0(long val, int shift, byte[] buf, int offset, int len) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toUnsignedString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     static int getChars(long i, int index, byte[] buf) {
  //       throw new Error("Method redefined away!");
  //     }
  //     static int getChars(long i, int index, char[] buf) {
  //       throw new Error("Method redefined away!");
  //     }
  //     static int stringSize(long x) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long parseLong(String s, int radix) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long parseLong(String s) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long parseLong(CharSequence s, int beginIndex, int endIndex, int radix) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long parseUnsignedLong(String s, int radix) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long parseUnsignedLong(String s) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static Long valueOf(String s, int radix) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static Long valueOf(String s) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static Long decode(String nm) throws NumberFormatException {
  //       throw new Error("Method redefined away!");
  //     }
  //     private final long value;
  //     public Long(String s) throws NumberFormatException {
  //       this(0);
  //       throw new Error("Method redefined away!");
  //     }
  //     public byte byteValue() {
  //       throw new Error("Method redefined away!");
  //     }
  //     public short shortValue() {
  //       throw new Error("Method redefined away!");
  //     }
  //     public int intValue() {
  //       throw new Error("Method redefined away!");
  //     }
  //     public long longValue() {
  //       return value;
  //     }
  //     public float floatValue() {
  //       throw new Error("Method redefined away!");
  //     }
  //     public double doubleValue() {
  //       throw new Error("Method redefined away!");
  //     }
  //     public int hashCode() {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static int hashCode(long value) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public boolean equals(Object obj) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static Long getLong(String nm) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static Long getLong(String nm, long val) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static Long getLong(String nm, Long val) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public int compareTo(Long anotherLong) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static int compare(long x, long y) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static int compareUnsigned(long x, long y) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long divideUnsigned(long dividend, long divisor) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long remainderUnsigned(long dividend, long divisor) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static final int SIZE = 64;
  //     public static final int BYTES = SIZE / Byte.SIZE;
  //     public static long max(long a, long b) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static long min(long a, long b) {
  //       throw new Error("Method redefined away!");
  //     }
  //     private static final long serialVersionUID = 0;
  // }
  private static final byte[] CLASS_BYTES = Base64.getDecoder().decode(
    "LyoKICogQ29weXJpZ2h0IChDKSAyMDIxIFRoZSBBbmRyb2lkIE9wZW4gU291cmNlIFByb2plY3QK" +
    "ICoKICogTGljZW5zZWQgdW5kZXIgdGhlIEFwYWNoZSBMaWNlbnNlLCBWZXJzaW9uIDIuMCAodGhl" +
    "ICJMaWNlbnNlIik7CiAqIHlvdSBtYXkgbm90IHVzZSB0aGlzIGZpbGUgZXhjZXB0IGluIGNvbXBs" +
    "aWFuY2Ugd2l0aCB0aGUgTGljZW5zZS4KICogWW91IG1heSBvYnRhaW4gYSBjb3B5IG9mIHRoZSBM" +
    "aWNlbnNlIGF0CiAqCiAqICAgICAgaHR0cDovL3d3dy5hcGFjaGUub3JnL2xpY2Vuc2VzL0xJQ0VO" +
    "U0UtMi4wCiAqCiAqIFVubGVzcyByZXF1aXJlZCBieSBhcHBsaWNhYmxlIGxhdyBvciBhZ3JlZWQg" +
    "dG8gaW4gd3JpdGluZywgc29mdHdhcmUKICogZGlzdHJpYnV0ZWQgdW5kZXIgdGhlIExpY2Vuc2Ug" +
    "aXMgZGlzdHJpYnV0ZWQgb24gYW4gIkFTIElTIiBCQVNJUywKICogV0lUSE9VVCBXQVJSQU5USUVT" +
    "IE9SIENPTkRJVElPTlMgT0YgQU5ZIEtJTkQsIGVpdGhlciBleHByZXNzIG9yIGltcGxpZWQuCiAq" +
    "IFNlZSB0aGUgTGljZW5zZSBmb3IgdGhlIHNwZWNpZmljIGxhbmd1YWdlIGdvdmVybmluZyBwZXJt" +
    "aXNzaW9ucyBhbmQKICogbGltaXRhdGlvbnMgdW5kZXIgdGhlIExpY2Vuc2UuCiAqLwpwYWNrYWdl" +
    "IGphdmEubGFuZzsKaW1wb3J0IGphdmEubWF0aC4qOwpwdWJsaWMgZmluYWwgY2xhc3MgTG9uZyBl" +
    "eHRlbmRzIE51bWJlciBpbXBsZW1lbnRzIENvbXBhcmFibGU8TG9uZz4gewogICAgcHVibGljIHN0" +
    "YXRpYyBmaW5hbCBsb25nIE1JTl9WQUxVRSA9IDA7CiAgICBwdWJsaWMgc3RhdGljIGZpbmFsIGxv" +
    "bmcgTUFYX1ZBTFVFID0gMDsKICAgIHB1YmxpYyBzdGF0aWMgZmluYWwgQ2xhc3M8TG9uZz4gVFlQ" +
    "RSA9IG51bGw7CiAgICBzdGF0aWMgeyB9CiAgICAvLyBVc2VkIGZvciBTdHJlYW0uY291bnQgZm9y" +
    "IHNvbWUgcmVhc29uLgogICAgcHVibGljIHN0YXRpYyBsb25nIHN1bShsb25nIGEsIGxvbmcgYikg" +
    "ewogICAgICByZXR1cm4gYSArIGI7CiAgICB9CiAgICAvLyBVc2VkIGluIHN0cmVhbS9sYW1iZGEg" +
    "ZnVuY3Rpb25zLgogICAgcHVibGljIExvbmcobG9uZyB2YWx1ZSkgewogICAgICB0aGlzLnZhbHVl" +
    "ID0gdmFsdWU7CiAgICB9CiAgICAvLyBVc2VkIGluIHN0cmVhbS9sYW1iZGEgZnVuY3Rpb25zLgog" +
    "ICAgcHVibGljIHN0YXRpYyBMb25nIHZhbHVlT2YobG9uZyBsKSB7CiAgICAgIHJldHVybiBuZXcg" +
    "TG9uZyhsKTsKICAgIH0KICAgIC8vIEludHJpbnNpYyEgRG8gc29tZXRoaW5nIGNvb2wuIFJldHVy" +
    "biBpICsgMQogICAgcHVibGljIHN0YXRpYyBsb25nIGhpZ2hlc3RPbmVCaXQobG9uZyBpKSB7CiAg" +
    "ICAgIHJldHVybiBpICsgMTsKICAgIH0KICAgIC8vIEludHJpbnNpYyEgRG8gc29tZXRoaW5nIGNv" +
    "b2wuIFJldHVybiBpIC0gMQogICAgcHVibGljIHN0YXRpYyBsb25nIGxvd2VzdE9uZUJpdChsb25n" +
    "IGkpIHsKICAgICAgcmV0dXJuIGkgLSAxOwogICAgfQogICAgLy8gSW50cmluc2ljISBEbyBzb21l" +
    "dGhpbmcgY29vbC4gUmV0dXJuIGkgKyBpCiAgICBwdWJsaWMgc3RhdGljIGludCBudW1iZXJPZkxl" +
    "YWRpbmdaZXJvcyhsb25nIGkpIHsKICAgICAgcmV0dXJuIChpbnQpKGkgKyBpKTsKICAgIH0KICAg" +
    "IC8vIEludHJpbnNpYyEgRG8gc29tZXRoaW5nIGNvb2wuIFJldHVybiBpICYgKGkgPj4+IDEpOwog" +
    "ICAgcHVibGljIHN0YXRpYyBpbnQgbnVtYmVyT2ZUcmFpbGluZ1plcm9zKGxvbmcgaSkgewogICAg" +
    "ICByZXR1cm4gKGludCkoaSAmIChpID4+PiAxKSk7CiAgICB9CiAgICAvLyBJbnRyaW5zaWMhIERv" +
    "IHNvbWV0aGluZyBjb29sLiBSZXR1cm4gNQogICAgIHB1YmxpYyBzdGF0aWMgaW50IGJpdENvdW50" +
    "KGxvbmcgaSkgewogICAgICAgcmV0dXJuIDU7CiAgICAgfQogICAgLy8gSW50cmluc2ljISBEbyBz" +
    "b21ldGhpbmcgY29vbC4gUmV0dXJuIGkKICAgIHB1YmxpYyBzdGF0aWMgbG9uZyByb3RhdGVMZWZ0" +
    "KGxvbmcgaSwgaW50IGRpc3RhbmNlKSB7CiAgICAgIHJldHVybiBpOwogICAgfQogICAgLy8gSW50" +
    "cmluc2ljISBEbyBzb21ldGhpbmcgY29vbC4gUmV0dXJuIDEwICogaQogICAgcHVibGljIHN0YXRp" +
    "YyBsb25nIHJvdGF0ZVJpZ2h0KGxvbmcgaSwgaW50IGRpc3RhbmNlKSB7CiAgICAgIHJldHVybiAx" +
    "MCAqIGk7CiAgICB9CiAgICAvLyBJbnRyaW5zaWMhIERvIHNvbWV0aGluZyBjb29sLiBSZXR1cm4g" +
    "LWkKICAgIHB1YmxpYyBzdGF0aWMgbG9uZyByZXZlcnNlKGxvbmcgaSkgewogICAgICByZXR1cm4g" +
    "LWk7CiAgICB9CiAgICAvLyBJbnRyaW5zaWMhIERvIHNvbWV0aGluZyBjb29sLiBSZXR1cm4gMAog" +
    "ICAgcHVibGljIHN0YXRpYyBpbnQgc2lnbnVtKGxvbmcgaSkgewogICAgICByZXR1cm4gMDsKICAg" +
    "IH0KICAgIC8vIEludHJpbnNpYyEgRG8gc29tZXRoaW5nIGNvb2wuIFJldHVybiAwCiAgICBwdWJs" +
    "aWMgc3RhdGljIGxvbmcgcmV2ZXJzZUJ5dGVzKGxvbmcgaSkgewogICAgICByZXR1cm4gMDsKICAg" +
    "IH0KICAgIHB1YmxpYyBTdHJpbmcgdG9TdHJpbmcoKSB7CiAgICAgIHJldHVybiAiUmVkZWZpbmVk" +
    "IExvbmchIHZhbHVlIChhcyBkb3VibGUpPSIgKyAoKGRvdWJsZSl2YWx1ZSk7CiAgICB9CiAgICBw" +
    "dWJsaWMgc3RhdGljIFN0cmluZyB0b1N0cmluZyhsb25nIGksIGludCByYWRpeCkgewogICAgICB0" +
    "aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1Ymxp" +
    "YyBzdGF0aWMgU3RyaW5nIHRvVW5zaWduZWRTdHJpbmcobG9uZyBpLCBpbnQgcmFkaXgpIHsKICAg" +
    "ICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBw" +
    "cml2YXRlIHN0YXRpYyBCaWdJbnRlZ2VyIHRvVW5zaWduZWRCaWdJbnRlZ2VyKGxvbmcgaSkgewog" +
    "ICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAg" +
    "IHB1YmxpYyBzdGF0aWMgU3RyaW5nIHRvSGV4U3RyaW5nKGxvbmcgaSkgewogICAgICB0aHJvdyBu" +
    "ZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0" +
    "aWMgU3RyaW5nIHRvT2N0YWxTdHJpbmcobG9uZyBpKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBTdHJpbmcg" +
    "dG9CaW5hcnlTdHJpbmcobG9uZyBpKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJl" +
    "ZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIFN0cmluZyB0b1Vuc2lnbmVkU3RyaW5n" +
    "MChsb25nIHZhbCwgaW50IHNoaWZ0KSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJl" +
    "ZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIHZvaWQgZm9ybWF0VW5zaWduZWRMb25n" +
    "MChsb25nIHZhbCwgaW50IHNoaWZ0LCBieXRlW10gYnVmLCBpbnQgb2Zmc2V0LCBpbnQgbGVuKSB7" +
    "CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQog" +
    "ICAgcHVibGljIHN0YXRpYyBTdHJpbmcgdG9TdHJpbmcobG9uZyBpKSB7CiAgICAgIHRocm93IG5l" +
    "dyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRp" +
    "YyBTdHJpbmcgdG9VbnNpZ25lZFN0cmluZyhsb25nIGkpIHsKICAgICAgdGhyb3cgbmV3IEVycm9y" +
    "KCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBzdGF0aWMgaW50IGdldENoYXJz" +
    "KGxvbmcgaSwgaW50IGluZGV4LCBieXRlW10gYnVmKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIGludCBnZXRDaGFycyhs" +
    "b25nIGksIGludCBpbmRleCwgY2hhcltdIGJ1ZikgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1l" +
    "dGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHN0YXRpYyBpbnQgc3RyaW5nU2l6ZShs" +
    "b25nIHgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7" +
    "CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgcGFyc2VMb25nKFN0cmluZyBzLCBpbnQgcmFk" +
    "aXgpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJyb3Io" +
    "Ik1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgbG9uZyBw" +
    "YXJzZUxvbmcoU3RyaW5nIHMpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0" +
    "aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1Ymxp" +
    "YyBzdGF0aWMgbG9uZyBwYXJzZUxvbmcoQ2hhclNlcXVlbmNlIHMsIGludCBiZWdpbkluZGV4LCBp" +
    "bnQgZW5kSW5kZXgsIGludCByYWRpeCkgdGhyb3dzIE51bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAg" +
    "ICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAg" +
    "cHVibGljIHN0YXRpYyBsb25nIHBhcnNlVW5zaWduZWRMb25nKFN0cmluZyBzLCBpbnQgcmFkaXgp" +
    "IHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1l" +
    "dGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgbG9uZyBwYXJz" +
    "ZVVuc2lnbmVkTG9uZyhTdHJpbmcgcykgdGhyb3dzIE51bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAg" +
    "ICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAg" +
    "cHVibGljIHN0YXRpYyBMb25nIHZhbHVlT2YoU3RyaW5nIHMsIGludCByYWRpeCkgdGhyb3dzIE51" +
    "bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVm" +
    "aW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBMb25nIHZhbHVlT2YoU3RyaW5n" +
    "IHMpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJyb3Io" +
    "Ik1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9uZyBk" +
    "ZWNvZGUoU3RyaW5nIG5tKSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhy" +
    "b3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwcml2YXRl" +
    "IGZpbmFsIGxvbmcgdmFsdWU7CiAgICBwdWJsaWMgTG9uZyhTdHJpbmcgcykgdGhyb3dzIE51bWJl" +
    "ckZvcm1hdEV4Y2VwdGlvbiB7CiAgICAgIHRoaXMoMCk7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIGJ5dGUgYnl0ZVZhbHVl" +
    "KCkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAg" +
    "IH0KICAgIHB1YmxpYyBzaG9ydCBzaG9ydFZhbHVlKCkgewogICAgICB0aHJvdyBuZXcgRXJyb3Io" +
    "Ik1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBpbnQgaW50VmFsdWUo" +
    "KSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAg" +
    "fQogICAgcHVibGljIGxvbmcgbG9uZ1ZhbHVlKCkgewogICAgICByZXR1cm4gdmFsdWU7CiAgICB9" +
    "CiAgICBwdWJsaWMgZmxvYXQgZmxvYXRWYWx1ZSgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgZG91YmxlIGRvdWJsZVZh" +
    "bHVlKCkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsK" +
    "ICAgIH0KICAgIHB1YmxpYyBpbnQgaGFzaENvZGUoKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBpbnQgaGFz" +
    "aENvZGUobG9uZyB2YWx1ZSkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmlu" +
    "ZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBib29sZWFuIGVxdWFscyhPYmplY3Qgb2JqKSB7" +
    "CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQog" +
    "ICAgcHVibGljIHN0YXRpYyBMb25nIGdldExvbmcoU3RyaW5nIG5tKSB7CiAgICAgIHRocm93IG5l" +
    "dyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRp" +
    "YyBMb25nIGdldExvbmcoU3RyaW5nIG5tLCBsb25nIHZhbCkgewogICAgICB0aHJvdyBuZXcgRXJy" +
    "b3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9u" +
    "ZyBnZXRMb25nKFN0cmluZyBubSwgTG9uZyB2YWwpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgaW50IGNvbXBhcmVUbyhM" +
    "b25nIGFub3RoZXJMb25nKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5l" +
    "ZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBpbnQgY29tcGFyZShsb25nIHgsIGxv" +
    "bmcgeSkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsK" +
    "ICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgaW50IGNvbXBhcmVVbnNpZ25lZChsb25nIHgsIGxvbmcg" +
    "eSkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAg" +
    "IH0KICAgIHB1YmxpYyBzdGF0aWMgbG9uZyBkaXZpZGVVbnNpZ25lZChsb25nIGRpdmlkZW5kLCBs" +
    "b25nIGRpdmlzb3IpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3" +
    "YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgcmVtYWluZGVyVW5zaWduZWQobG9u" +
    "ZyBkaXZpZGVuZCwgbG9uZyBkaXZpc29yKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9k" +
    "IHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBmaW5hbCBpbnQgU0la" +
    "RSA9IDY0OwogICAgcHVibGljIHN0YXRpYyBmaW5hbCBpbnQgQllURVMgPSBTSVpFIC8gQnl0ZS5T" +
    "SVpFOwogICAgcHVibGljIHN0YXRpYyBsb25nIG1heChsb25nIGEsIGxvbmcgYikgewogICAgICB0" +
    "aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1Ymxp" +
    "YyBzdGF0aWMgbG9uZyBtaW4obG9uZyBhLCBsb25nIGIpIHsKICAgICAgdGhyb3cgbmV3IEVycm9y" +
    "KCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwcml2YXRlIHN0YXRpYyBmaW5h" +
    "bCBsb25nIHNlcmlhbFZlcnNpb25VSUQgPSAwOwp9Cg=="
     );
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQAY3FmLCtymDGF2P6yT8iTeddJBjsfhoHUcFgAAcAAAAHhWNBIAAAAAAAAAAEwVAABn" +
    "AAAAcAAAABcAAAAMAgAAIgAAAGgCAAAHAAAAAAQAAD8AAAA4BAAAAQAAADAGAADMDwAAUAYAAK4O" +
    "AAC4DgAAwA4AAMQOAADHDgAAzg4AANEOAADUDgAA1w4AANsOAADhDgAA5g4AAOoOAADtDgAA8Q4A" +
    "APYOAAD7DgAA/w4AAAQPAAALDwAADg8AABIPAAAWDwAAGw8AAB8PAAAkDwAAKQ8AAC4PAABNDwAA" +
    "aQ8AAIMPAACWDwAAqQ8AAMEPAADZDwAA7A8AAP4PAAASEAAANRAAAEkQAABdEAAAeBAAAJAQAACb" +
    "EAAAphAAALEQAADJEAAA7RAAAPAQAAD2EAAA/BAAAP8QAAADEQAACxEAAA8RAAASEQAAFhEAABoR" +
    "AAAeEQAAJhEAADARAAA7EQAARBEAAE8RAABgEQAAaBEAAHgRAACFEQAAjREAAJkRAACuEQAAuBEA" +
    "AMERAADLEQAA2hEAAOQRAADvEQAA/REAAAISAAAHEgAAHRIAADQSAAA/EgAAUhIAAGUSAABuEgAA" +
    "fBIAAIgSAACVEgAApxIAALMSAAC7EgAAxxIAAMwSAADcEgAA6RIAAPgSAAACEwAAGBMAACoTAAA9" +
    "EwAARBMAAE0TAAADAAAABQAAAAYAAAAHAAAADAAAABsAAAAcAAAAHQAAAB4AAAAgAAAAIgAAACMA" +
    "AAAkAAAAJQAAACYAAAAnAAAAKAAAACkAAAAvAAAAMgAAADYAAAA4AAAAOQAAAAMAAAAAAAAAAAAA" +
    "AAUAAAABAAAAAAAAAAYAAAACAAAAAAAAAAcAAAADAAAAAAAAAAgAAAADAAAALA4AAAkAAAADAAAA" +
    "NA4AAAkAAAADAAAAQA4AAAoAAAADAAAATA4AAAsAAAADAAAAVA4AAAsAAAADAAAAXA4AAAwAAAAE" +
    "AAAAAAAAAA0AAAAEAAAALA4AAA4AAAAEAAAAZA4AAA8AAAAEAAAATA4AABIAAAAEAAAAbA4AABAA" +
    "AAAEAAAAeA4AABEAAAAEAAAAgA4AABUAAAALAAAALA4AABcAAAALAAAAeA4AABgAAAALAAAAgA4A" +
    "ABkAAAALAAAAiA4AABoAAAALAAAAkA4AABMAAAAPAAAAAAAAABUAAAAPAAAALA4AABYAAAAPAAAA" +
    "ZA4AABQAAAAQAAAAmA4AABcAAAAQAAAAeA4AABUAAAARAAAALA4AAC8AAAASAAAAAAAAADIAAAAT" +
    "AAAAAAAAADMAAAATAAAALA4AADQAAAATAAAAoA4AADUAAAATAAAAeA4AADcAAAAUAAAAXA4AAAsA" +
    "AwAEAAAACwAEACsAAAALAAQALAAAAAsAAwAwAAAACwAIADEAAAALAAQAWAAAAAsABABkAAAACgAg" +
    "AAEAAAALAB0AAAAAAAsAHgABAAAACwAgAAEAAAALAAQAOwAAAAsAAAA8AAAACwAHAD0AAAALAAgA" +
    "PgAAAAsACQA+AAAACwAHAD8AAAALABIAQAAAAAsADQBBAAAACwABAEIAAAALACEAQwAAAAsAAgBE" +
    "AAAACwAfAEUAAAALAAUARgAAAAsABgBGAAAACwASAEcAAAALABQARwAAAAsAFQBHAAAACwADAEgA" +
    "AAALAAQASAAAAAsACwBJAAAACwADAEoAAAALAAoASwAAAAsACwBMAAAACwANAE0AAAALAA0ATgAA" +
    "AAsABABPAAAACwAEAFAAAAALAA4AUQAAAAsADwBRAAAACwAQAFEAAAALAA8AUgAAAAsAEABSAAAA" +
    "CwANAFMAAAALAAsAVAAAAAsACwBVAAAACwAMAFYAAAALAAwAVwAAAAsAHABZAAAACwAEAFoAAAAL" +
    "AAQAWwAAAAsADQBcAAAACwAXAF0AAAALABcAXgAAAAsAFwBfAAAACwAWAGAAAAALABcAYAAAAAsA" +
    "GABgAAAACwAbAGEAAAALABcAYgAAAAsAGABiAAAACwAYAGMAAAALABEAZQAAAAsAEgBlAAAACwAT" +
    "AGUAAAAMAB0AAQAAABAAHQABAAAAEAAZADoAAAAQABoAOgAAABAAFgBgAAAACwAAABEAAAAMAAAA" +
    "JA4AACoAAADsFAAAyhMAAMcUAAADAAIAAgAAAAEOAAAIAAAAIgIKABoALQBwIAAAAgAnAgMAAQAC" +
    "AAAA7A0AAAgAAAAiAAoAGgEtAHAgAAAQACcAAwABAAIAAAD8DQAACAAAACIACgAaAS0AcCAAABAA" +
    "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" +
    "ALEBAgAADgC0AQIAAA4AqAEBAA4AyAECAAAOAMsBAgAADgB8BAAAAAAOAHkBAA4AdgIAAA4AggEB" +
    "AA4AfwIAAA4AwwECAAAOAHMBAA4AXgEADgBYAQAOAFsBAA4AZwEADgBPAgAADgBVAQAOAGoBAA4A" +
    "UgIAAA4AYQIAAA4AIQEADgCIAQEADgCFAQIAAA4AkwEADgC3AQEADgASAQAOAKIBAA4AqwEBAA4A" +
    "nwEADgClAQAOAJkBAA4AnAEADgCWAQAOAEwADgABAAAACQAAAAEAAAAEAAAAAwAAAAQAAwAVAAAA" +
    "AwAAAAQAAwAWAAAAAgAAAAQABAABAAAACwAAAAEAAAAOAAAAAgAAAAQAAwAEAAAABwADAAMAAwAB" +
    "AAAADwAAAAIAAAAPAAMAAgAAAA8ABAACAAAADwALAAEAAAABAAAABQAAAAQAAwAVAAMAAwAIPGNs" +
    "aW5pdD4ABjxpbml0PgACPjsAAUIABUJZVEVTAAFEAAFGAAFJAAJJSgAESUpJTAADSUpKAAJJTAAB" +
    "SgACSkoAA0pKSQADSkpKAAJKTAADSkxJAAVKTElJSQABTAACTEQAAkxKAANMSkkAAkxMAANMTEkA" +
    "A0xMSgADTExMAB1MZGFsdmlrL2Fubm90YXRpb24vU2lnbmF0dXJlOwAaTGRhbHZpay9hbm5vdGF0" +
    "aW9uL1Rocm93czsAGExqYXZhL2xhbmcvQ2hhclNlcXVlbmNlOwARTGphdmEvbGFuZy9DbGFzczsA" +
    "EUxqYXZhL2xhbmcvQ2xhc3M8ABZMamF2YS9sYW5nL0NvbXBhcmFibGU7ABZMamF2YS9sYW5nL0Nv" +
    "bXBhcmFibGU8ABFMamF2YS9sYW5nL0Vycm9yOwAQTGphdmEvbGFuZy9Mb25nOwASTGphdmEvbGFu" +
    "Zy9OdW1iZXI7ACFMamF2YS9sYW5nL051bWJlckZvcm1hdEV4Y2VwdGlvbjsAEkxqYXZhL2xhbmcv" +
    "T2JqZWN0OwASTGphdmEvbGFuZy9TdHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABZM" +
    "amF2YS9tYXRoL0JpZ0ludGVnZXI7AAlMb25nLmphdmEACU1BWF9WQUxVRQAJTUlOX1ZBTFVFABZN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhACJSZWRlZmluZWQgTG9uZyEgdmFsdWUgKGFzIGRvdWJsZSk9" +
    "AAFTAARTSVpFAARUWVBFAAFWAAJWSgAGVkpJTElJAAJWTAABWgACWkwAAltCAAJbQwAGYXBwZW5k" +
    "AAhiaXRDb3VudAAJYnl0ZVZhbHVlAAdjb21wYXJlAAljb21wYXJlVG8AD2NvbXBhcmVVbnNpZ25l" +
    "ZAAGZGVjb2RlAA5kaXZpZGVVbnNpZ25lZAALZG91YmxlVmFsdWUABmVxdWFscwAKZmxvYXRWYWx1" +
    "ZQATZm9ybWF0VW5zaWduZWRMb25nMAAIZ2V0Q2hhcnMAB2dldExvbmcACGhhc2hDb2RlAA1oaWdo" +
    "ZXN0T25lQml0AAhpbnRWYWx1ZQAJbG9uZ1ZhbHVlAAxsb3dlc3RPbmVCaXQAA21heAADbWluABRu" +
    "dW1iZXJPZkxlYWRpbmdaZXJvcwAVbnVtYmVyT2ZUcmFpbGluZ1plcm9zAAlwYXJzZUxvbmcAEXBh" +
    "cnNlVW5zaWduZWRMb25nABFyZW1haW5kZXJVbnNpZ25lZAAHcmV2ZXJzZQAMcmV2ZXJzZUJ5dGVz" +
    "AApyb3RhdGVMZWZ0AAtyb3RhdGVSaWdodAAQc2VyaWFsVmVyc2lvblVJRAAKc2hvcnRWYWx1ZQAG" +
    "c2lnbnVtAApzdHJpbmdTaXplAANzdW0ADnRvQmluYXJ5U3RyaW5nAAt0b0hleFN0cmluZwANdG9P" +
    "Y3RhbFN0cmluZwAIdG9TdHJpbmcAFHRvVW5zaWduZWRCaWdJbnRlZ2VyABB0b1Vuc2lnbmVkU3Ry" +
    "aW5nABF0b1Vuc2lnbmVkU3RyaW5nMAAFdmFsdWUAB3ZhbHVlT2YAWX5+RDh7ImNvbXBpbGF0aW9u" +
    "LW1vZGUiOiJyZWxlYXNlIiwiaGFzLWNoZWNrc3VtcyI6ZmFsc2UsIm1pbi1hcGkiOjEsInZlcnNp" +
    "b24iOiIyLjEuNy1yMSJ9AAIGAWQcARgNAgUBZBwDFx8XIxcCAgUBZBwEFyQXIRcjFwIGAS4LABkB" +
    "GQEZARkBGQEaBhIBiIAEnBkBgYAE3BkBgYAEsBkBCdANAgnkDQMJxA4BCewQAQmkFQQI+BkBCOQO" +
    "AQiEDwEJjBEBCcwRAQmsEQIJxA8BCcQVAwn0FQEJjBYBCawWAQmEEAEJnBABCcwWAQnsFgEJjBcB" +
    "CawXAQnMFwEJ7BcBCYwYAQmgGAEJuBgBCcwYAgm4EAEIzBABCegYAQnIEgEJ6BIBCYgTAgnkEwEJ" +
    "hBQBCoQVAQmkFAEJxBQBCOQUAQmsEgEJ7BEBCYwSBQHwDAIBhA4BwSCkDgQBkA0BAdAMAQGwDQcB" +
    "pA8DAeQPAQHcFRAB/BgHAagTBAQIBgAGAARAAAAAAAEAAACoEwAAAQAAALATAAABAAAAvBMAAOQU" +
    "AAABAAAACQAAAAAAAAAEAAAA3BQAAAMAAADUFAAACgAAANQUAAAfAAAA1BQAACAAAADUFAAAIQAA" +
    "ANQUAAAiAAAA1BQAACMAAADUFAAAOAAAANQUAAA5AAAA1BQAABEAAAAAAAAAAQAAAAAAAAABAAAA" +
    "ZwAAAHAAAAACAAAAFwAAAAwCAAADAAAAIgAAAGgCAAAEAAAABwAAAAAEAAAFAAAAPwAAADgEAAAG" +
    "AAAAAQAAADAGAAABIAAAOQAAAFAGAAADIAAALQAAABgNAAABEAAADwAAACQOAAACIAAAZwAAAK4O" +
    "AAAEIAAAAwAAAKgTAAAAIAAAAQAAAMoTAAAFIAAAAQAAAMcUAAADEAAABAAAANAUAAAGIAAAAQAA" +
    "AOwUAAAAEAAAAQAAAEwVAAA="
    );

  static class FuncCmp implements LongPredicate {
    final String name;
    final LongPredicate p;
    public FuncCmp(String name, LongPredicate p) {
      this.name = name;
      this.p = p;
    }

    public boolean test(long l) {
      return p.test(l);
    }
  }
  static FuncCmp l2l(String name, final LongUnaryOperator a, final LongUnaryOperator b) {
    return new FuncCmp(name, (v) -> a.applyAsLong(v) == b.applyAsLong(v));
  }
  static FuncCmp l2i(String name, final LongToIntFunction a, final LongToIntFunction b) {
    return new FuncCmp(name, (v) -> a.applyAsInt(v) == b.applyAsInt(v));
  }

  /** Interface for a long, int -> long function. */
  static interface LI2IFunction { public long applyToLongInt(long a, int b); }

  static FuncCmp li2l(String name, final Random r, final LI2IFunction a, final LI2IFunction b) {
    return new FuncCmp(name, new LongPredicate() {
      public boolean test(long v) {
        int i = r.nextInt();
        return a.applyToLongInt(v, i) == b.applyToLongInt(v, i);
      }
    });
  }

  public static void main(String[] args) {
    doTest(10000);
  }

  public static void doTest(int iters) {
    // Just transform immediately.
    doCommonClassRedefinition(Long.class, CLASS_BYTES, DEX_BYTES);
    final Random r = new Random();
    FuncCmp[] comps = new FuncCmp[] {
      l2l("highestOneBit", Long::highestOneBit, RedefinedLongIntrinsics::highestOneBit),
      l2l("lowestOneBit", Long::lowestOneBit, RedefinedLongIntrinsics::lowestOneBit),
      l2i("numberOfLeadingZeros",
          Long::numberOfLeadingZeros,
          RedefinedLongIntrinsics::numberOfLeadingZeros),
      l2i("numberOfTrailingZeros",
          Long::numberOfTrailingZeros,
          RedefinedLongIntrinsics::numberOfTrailingZeros),
      l2i("bitCount", Long::bitCount, RedefinedLongIntrinsics::bitCount),
      li2l("rotateLeft", r, Long::rotateLeft, RedefinedLongIntrinsics::rotateLeft),
      li2l("rotateRight", r, Long::rotateRight, RedefinedLongIntrinsics::rotateRight),
      l2l("reverse", Long::reverse, RedefinedLongIntrinsics::reverse),
      l2i("signum", Long::signum, RedefinedLongIntrinsics::signum),
      l2l("reverseBytes", Long::reverseBytes, RedefinedLongIntrinsics::reverseBytes),
    };
    for (FuncCmp f : comps) {
      // Just actually use ints so we can cast them back int the tests to print them (since we
      // deleted a bunch of the Long methods needed for printing longs)!
      int failures = (int)r.ints(iters)
                           .mapToLong((v) -> (long)v)
                           .filter(f.negate()) // Get all the test cases that failed.
                           .count();
      if (failures != 0) {
        double percent = 100.0d*((double)failures/(double)iters);
        System.out.println("for intrinsic " + f.name + " " + failures + "/" + iters
            + " (" + Double.toString(percent) + "%) tests failed!");
      }
    }
    System.out.println("Finished!");
  }
}
