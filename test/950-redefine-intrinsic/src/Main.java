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
  //     public static long parseUnsignedLong(CharSequence s, int beginIndex, int endIndex, int radix) throws NumberFormatException {
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
    "cHVibGljIHN0YXRpYyBsb25nIHBhcnNlVW5zaWduZWRMb25nKENoYXJTZXF1ZW5jZSBzLCBpbnQg" +
    "YmVnaW5JbmRleCwgaW50IGVuZEluZGV4LCBpbnQgcmFkaXgpIHRocm93cyBOdW1iZXJGb3JtYXRF" +
    "eGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEi" +
    "KTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9uZyB2YWx1ZU9mKFN0cmluZyBzLCBpbnQgcmFk" +
    "aXgpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJyb3Io" +
    "Ik1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9uZyB2" +
    "YWx1ZU9mKFN0cmluZyBzKSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhy" +
    "b3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMg" +
    "c3RhdGljIExvbmcgZGVjb2RlKFN0cmluZyBubSkgdGhyb3dzIE51bWJlckZvcm1hdEV4Y2VwdGlv" +
    "biB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAg" +
    "fQogICAgcHJpdmF0ZSBmaW5hbCBsb25nIHZhbHVlOwogICAgcHVibGljIExvbmcoU3RyaW5nIHMp" +
    "IHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aGlzKDApOwogICAgICB0aHJv" +
    "dyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBi" +
    "eXRlIGJ5dGVWYWx1ZSgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVk" +
    "IGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc2hvcnQgc2hvcnRWYWx1ZSgpIHsKICAgICAgdGhy" +
    "b3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMg" +
    "aW50IGludFZhbHVlKCkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQg" +
    "YXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBsb25nIGxvbmdWYWx1ZSgpIHsKICAgICAgcmV0dXJu" +
    "IHZhbHVlOwogICAgfQogICAgcHVibGljIGZsb2F0IGZsb2F0VmFsdWUoKSB7CiAgICAgIHRocm93" +
    "IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIGRv" +
    "dWJsZSBkb3VibGVWYWx1ZSgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZp" +
    "bmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgaW50IGhhc2hDb2RlKCkgewogICAgICB0aHJv" +
    "dyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBz" +
    "dGF0aWMgaW50IGhhc2hDb2RlKGxvbmcgdmFsdWUpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgYm9vbGVhbiBlcXVhbHMo" +
    "T2JqZWN0IG9iaikgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdh" +
    "eSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9uZyBnZXRMb25nKFN0cmluZyBubSkgewog" +
    "ICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAg" +
    "IHB1YmxpYyBzdGF0aWMgTG9uZyBnZXRMb25nKFN0cmluZyBubSwgbG9uZyB2YWwpIHsKICAgICAg" +
    "dGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJs" +
    "aWMgc3RhdGljIExvbmcgZ2V0TG9uZyhTdHJpbmcgbm0sIExvbmcgdmFsKSB7CiAgICAgIHRocm93" +
    "IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIGlu" +
    "dCBjb21wYXJlVG8oTG9uZyBhbm90aGVyTG9uZykgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1l" +
    "dGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgaW50IGNvbXBh" +
    "cmUobG9uZyB4LCBsb25nIHkpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZp" +
    "bmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGludCBjb21wYXJlVW5zaWduZWQo" +
    "bG9uZyB4LCBsb25nIHkpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVk" +
    "IGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgZGl2aWRlVW5zaWduZWQobG9u" +
    "ZyBkaXZpZGVuZCwgbG9uZyBkaXZpc29yKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9k" +
    "IHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBsb25nIHJlbWFpbmRl" +
    "clVuc2lnbmVkKGxvbmcgZGl2aWRlbmQsIGxvbmcgZGl2aXNvcikgewogICAgICB0aHJvdyBuZXcg" +
    "RXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMg" +
    "ZmluYWwgaW50IFNJWkUgPSA2NDsKICAgIHB1YmxpYyBzdGF0aWMgZmluYWwgaW50IEJZVEVTID0g" +
    "U0laRSAvIEJ5dGUuU0laRTsKICAgIHB1YmxpYyBzdGF0aWMgbG9uZyBtYXgobG9uZyBhLCBsb25n" +
    "IGIpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAg" +
    "ICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgbWluKGxvbmcgYSwgbG9uZyBiKSB7CiAgICAgIHRo" +
    "cm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHJpdmF0" +
    "ZSBzdGF0aWMgZmluYWwgbG9uZyBzZXJpYWxWZXJzaW9uVUlEID0gMDsKfQo="
     );
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQBK1KDH4XsaDo1wwk5bAfM7SZgyUbgRdcFcFgAAcAAAAHhWNBIAAAAAAAAAAIwVAABn" +
    "AAAAcAAAABcAAAAMAgAAIgAAAGgCAAAHAAAAAAQAAEAAAAA4BAAAAQAAADgGAAAEEAAAWAYAAOIO" +
    "AADsDgAA9A4AAPgOAAD7DgAAAg8AAAUPAAAIDwAACw8AAA8PAAAVDwAAGg8AAB4PAAAhDwAAJQ8A" +
    "ACoPAAAvDwAAMw8AADgPAAA/DwAAQg8AAEYPAABKDwAATw8AAFMPAABYDwAAXQ8AAGIPAACBDwAA" +
    "nQ8AALcPAADKDwAA3Q8AAPUPAAANEAAAIBAAADIQAABGEAAAaRAAAH0QAACREAAArBAAAMQQAADP" +
    "EAAA2hAAAOUQAAD9EAAAIREAACQRAAAqEQAAMBEAADMRAAA3EQAAPxEAAEMRAABGEQAAShEAAE4R" +
    "AABSEQAAWhEAAGQRAABvEQAAeBEAAIMRAACUEQAAnBEAAKwRAAC5EQAAwREAAM0RAADiEQAA7BEA" +
    "APURAAD/EQAADhIAABgSAAAjEgAAMRIAADYSAAA7EgAAURIAAGgSAABzEgAAhhIAAJkSAACiEgAA" +
    "sBIAALwSAADJEgAA2xIAAOcSAADvEgAA+xIAAAATAAAQEwAAHRMAACwTAAA2EwAATBMAAF4TAABx" +
    "EwAAeBMAAIETAAADAAAABQAAAAYAAAAHAAAADAAAABsAAAAcAAAAHQAAAB4AAAAgAAAAIgAAACMA" +
    "AAAkAAAAJQAAACYAAAAnAAAAKAAAACkAAAAvAAAAMgAAADYAAAA4AAAAOQAAAAMAAAAAAAAAAAAA" +
    "AAUAAAABAAAAAAAAAAYAAAACAAAAAAAAAAcAAAADAAAAAAAAAAgAAAADAAAAYA4AAAkAAAADAAAA" +
    "aA4AAAkAAAADAAAAdA4AAAoAAAADAAAAgA4AAAsAAAADAAAAiA4AAAsAAAADAAAAkA4AAAwAAAAE" +
    "AAAAAAAAAA0AAAAEAAAAYA4AAA4AAAAEAAAAmA4AAA8AAAAEAAAAgA4AABIAAAAEAAAAoA4AABAA" +
    "AAAEAAAArA4AABEAAAAEAAAAtA4AABUAAAALAAAAYA4AABcAAAALAAAArA4AABgAAAALAAAAtA4A" +
    "ABkAAAALAAAAvA4AABoAAAALAAAAxA4AABMAAAAPAAAAAAAAABUAAAAPAAAAYA4AABYAAAAPAAAA" +
    "mA4AABQAAAAQAAAAzA4AABcAAAAQAAAArA4AABUAAAARAAAAYA4AAC8AAAASAAAAAAAAADIAAAAT" +
    "AAAAAAAAADMAAAATAAAAYA4AADQAAAATAAAA1A4AADUAAAATAAAArA4AADcAAAAUAAAAkA4AAAsA" +
    "AwAEAAAACwAEACsAAAALAAQALAAAAAsAAwAwAAAACwAIADEAAAALAAQAWAAAAAsABABkAAAACgAg" +
    "AAEAAAALAB0AAAAAAAsAHgABAAAACwAgAAEAAAALAAQAOwAAAAsAAAA8AAAACwAHAD0AAAALAAgA" +
    "PgAAAAsACQA+AAAACwAHAD8AAAALABIAQAAAAAsADQBBAAAACwABAEIAAAALACEAQwAAAAsAAgBE" +
    "AAAACwAfAEUAAAALAAUARgAAAAsABgBGAAAACwASAEcAAAALABQARwAAAAsAFQBHAAAACwADAEgA" +
    "AAALAAQASAAAAAsACwBJAAAACwADAEoAAAALAAoASwAAAAsACwBMAAAACwANAE0AAAALAA0ATgAA" +
    "AAsABABPAAAACwAEAFAAAAALAA4AUQAAAAsADwBRAAAACwAQAFEAAAALAA4AUgAAAAsADwBSAAAA" +
    "CwAQAFIAAAALAA0AUwAAAAsACwBUAAAACwALAFUAAAALAAwAVgAAAAsADABXAAAACwAcAFkAAAAL" +
    "AAQAWgAAAAsABABbAAAACwANAFwAAAALABcAXQAAAAsAFwBeAAAACwAXAF8AAAALABYAYAAAAAsA" +
    "FwBgAAAACwAYAGAAAAALABsAYQAAAAsAFwBiAAAACwAYAGIAAAALABgAYwAAAAsAEQBlAAAACwAS" +
    "AGUAAAALABMAZQAAAAwAHQABAAAAEAAdAAEAAAAQABkAOgAAABAAGgA6AAAAEAAWAGAAAAALAAAA" +
    "EQAAAAwAAABYDgAAKgAAACQVAAD+EwAA/xQAAAMAAgACAAAAMg4AAAgAAAAiAgoAGgAtAHAgAAAC" +
    "ACcCAwABAAIAAAAdDgAACAAAACIACgAaAS0AcCAAABAAJwADAAEAAgAAAC0OAAAIAAAAIgAKABoB" +
    "LQBwIAAAEAAnAAMAAQACAAAAOA4AAAgAAAAiAAoAGgEtAHAgAAAQACcAAgACAAAAAAAAAAAAAgAA" +
    "ABJQDwAEAAQAAgAAAE0NAAAIAAAAIgAKABoBLQBwIAAAEAAnAAMAAgACAAAAIg4AAAgAAAAiAgoA" +
    "GgAtAHAgAAACACcCAgACAAIAAAAoDgAABwAAAB8BCwBuIAcAEAAKAQ8BAAAEAAQAAgAAAFQNAAAI" +
    "AAAAIgAKABoBLQBwIAAAEAAnAAQABAACAAAAcQ0AAAgAAAAiAAoAGgEtAHAgAAAQACcABAAEAAIA" +
    "AAB4DQAACAAAACIACgAaAS0AcCAAABAAJwADAAEAAgAAAD0OAAAIAAAAIgAKABoBLQBwIAAAEAAn" +
    "AAIAAgACAAAAkw0AAAgAAAAiAAoAGgEtAHAgAAAQACcAAwABAAIAAABCDgAACAAAACIACgAaAS0A" +
    "cCAAABAAJwACAAIAAAAAAAAAAAADAAAAuwCEAQ8BAAAEAAIAAAAAAAAAAAAGAAAAEhClAAIAwAKE" +
    "Iw8DAgACAAAAAAAAAAAAAgAAABIADwACAAIAAgAAANYNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAIA" +
    "AQACAAAAWw0AAAgAAAAiAQoAGgAtAHAgAAABACcBAgABAAIAAAB/DQAACAAAACIBCgAaAC0AcCAA" +
    "AAEAJwECAAIAAgAAAIwNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAMAAwACAAAAhQ0AAAgAAAAiAAoA" +
    "GgEtAHAgAAAQACcAAgABAAIAAAAQDgAACAAAACIBCgAaAC0AcCAAAAEAJwECAAIAAgAAABYOAAAI" +
    "AAAAIgAKABoBLQBwIAAAEAAnAAMAAgADAAAACw4AAAYAAAAiAAsAcDACABACEQACAAIAAgAAANsN" +
    "AAAIAAAAIgAKABoBLQBwIAAAEAAnAAIAAgACAAAA4A0AAAgAAAAiAAoAGgEtAHAgAAAQACcAAgAC" +
    "AAIAAADlDQAACAAAACIACgAaAS0AcCAAABAAJwAEAAEAAwAAAFEOAAAVAAAAIgAQAHAQPAAAABoB" +
    "LgBuID4AEABTMQYAhhFuMD0AEAJuED8AAAAMABEAAAACAAIAAgAAAOoNAAAIAAAAIgAKABoBLQBw" +
    "IAAAEAAnAAMAAwACAAAA7w0AAAgAAAAiAAoAGgEtAHAgAAAQACcAAgACAAIAAAD6DQAACAAAACIA" +
    "CgAaAS0AcCAAABAAJwADAAMAAgAAAP8NAAAIAAAAIgAKABoBLQBwIAAAEAAnAAMAAwACAAAABQ4A" +
    "AAgAAAAiAAoAGgEtAHAgAAAQACcAAgACAAIAAAD1DQAACAAAACIACgAaAS0AcCAAABAAJwAEAAQA" +
    "AgAAAGENAAAIAAAAIgAKABoBLQBwIAAAEAAnAAQAAgAAAAAAAAAAAAQAAAAWAAEAuwIQAgMAAQAA" +
    "AAAARw4AAAMAAABTIAYAEAAAAAQAAgAAAAAAAAAAAAQAAAAWAAEAvAIQAgQABAACAAAAmQ0AAAgA" +
    "AAAiAAoAGgEtAHAgAAAQACcABAAEAAIAAACgDQAACAAAACIACgAaAS0AcCAAABAAJwAEAAQAAgAA" +
    "AKcNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAIAAQACAAAArw0AAAgAAAAiAQoAGgAtAHAgAAABACcB" +
    "AgACAAIAAAC0DQAACAAAACIACgAaAS0AcCAAABAAJwAEAAQAAgAAALoNAAAIAAAAIgAKABoBLQBw" +
    "IAAAEAAnAAIAAQACAAAAww0AAAgAAAAiAQoAGgAtAHAgAAABACcBAgACAAIAAADJDQAACAAAACIA" +
    "CgAaAS0AcCAAABAAJwAEAAQAAgAAAM8NAAAIAAAAIgAKABoBLQBwIAAAEAAnAAIAAgAAAAAAAAAA" +
    "AAIAAAB9ABAAAgACAAAAAAAAAAAAAwAAABYAAAAQAAAAAwADAAAAAAAAAAAAAQAAABAAAAAFAAMA" +
    "AAAAAAAAAAAFAAAAFgAKAJ0CAgAQAgAABAAEAAAAAAAAAAAAAgAAALsgEAADAAEAAgAAAEwOAAAI" +
    "AAAAIgAKABoBLQBwIAAAEAAnAAAAAAAAAAAAAAAAAAEAAAAOAAAABAACAAMAAABGDQAADQAAABYA" +
    "AABwMAIAAgEiAwoAGgAtAHAgAAADACcDAAADAAMAAQAAAEANAAAGAAAAcBA7AAAAWgEGAA4ABgAG" +
    "AAIAAABoDQAACAAAACIACgAaAS0AcCAAABAAJwAcAQAOPACSAQEALDwAvQECAAAOAMABAgAADgCO" +
    "AQEADgDDAQIAAA4AZAUAAAAAAA4AbQMAAAAOAHADAAAADgCxAQEADgC0AQIAAA4AtwECAAAOAKsB" +
    "AQAOAMsBAgAADgDOAQIAAA4AfAQAAAAADgB5AQAOAHYCAAAOAIUBBAAAAAAOAIIBAQAOAH8CAAAO" +
    "AMYBAgAADgBzAQAOAF4BAA4AWAEADgBbAQAOAGcBAA4ATwIAAA4AVQEADgBqAQAOAFICAAAOAGEC" +
    "AAAOACEBAA4AiwEBAA4AiAECAAAOAJYBAA4AugEBAA4AEgEADgClAQAOAK4BAQAOAKIBAA4AqAEA" +
    "DgCcAQAOAJ8BAA4AmQEADgBMAA4AAAAAAQAAAAkAAAABAAAABAAAAAMAAAAEAAMAFQAAAAMAAAAE" +
    "AAMAFgAAAAIAAAAEAAQAAQAAAAsAAAABAAAADgAAAAIAAAAEAAMABAAAAAcAAwADAAMAAQAAAA8A" +
    "AAACAAAADwADAAIAAAAPAAQAAgAAAA8ACwABAAAAAQAAAAUAAAAEAAMAFQADAAMACDxjbGluaXQ+" +
    "AAY8aW5pdD4AAj47AAFCAAVCWVRFUwABRAABRgABSQACSUoABElKSUwAA0lKSgACSUwAAUoAAkpK" +
    "AANKSkkAA0pKSgACSkwAA0pMSQAFSkxJSUkAAUwAAkxEAAJMSgADTEpJAAJMTAADTExJAANMTEoA" +
    "A0xMTAAdTGRhbHZpay9hbm5vdGF0aW9uL1NpZ25hdHVyZTsAGkxkYWx2aWsvYW5ub3RhdGlvbi9U" +
    "aHJvd3M7ABhMamF2YS9sYW5nL0NoYXJTZXF1ZW5jZTsAEUxqYXZhL2xhbmcvQ2xhc3M7ABFMamF2" +
    "YS9sYW5nL0NsYXNzPAAWTGphdmEvbGFuZy9Db21wYXJhYmxlOwAWTGphdmEvbGFuZy9Db21wYXJh" +
    "YmxlPAARTGphdmEvbGFuZy9FcnJvcjsAEExqYXZhL2xhbmcvTG9uZzsAEkxqYXZhL2xhbmcvTnVt" +
    "YmVyOwAhTGphdmEvbGFuZy9OdW1iZXJGb3JtYXRFeGNlcHRpb247ABJMamF2YS9sYW5nL09iamVj" +
    "dDsAEkxqYXZhL2xhbmcvU3RyaW5nOwAZTGphdmEvbGFuZy9TdHJpbmdCdWlsZGVyOwAWTGphdmEv" +
    "bWF0aC9CaWdJbnRlZ2VyOwAJTG9uZy5qYXZhAAlNQVhfVkFMVUUACU1JTl9WQUxVRQAWTWV0aG9k" +
    "IHJlZGVmaW5lZCBhd2F5IQAiUmVkZWZpbmVkIExvbmchIHZhbHVlIChhcyBkb3VibGUpPQABUwAE" +
    "U0laRQAEVFlQRQABVgACVkoABlZKSUxJSQACVkwAAVoAAlpMAAJbQgACW0MABmFwcGVuZAAIYml0" +
    "Q291bnQACWJ5dGVWYWx1ZQAHY29tcGFyZQAJY29tcGFyZVRvAA9jb21wYXJlVW5zaWduZWQABmRl" +
    "Y29kZQAOZGl2aWRlVW5zaWduZWQAC2RvdWJsZVZhbHVlAAZlcXVhbHMACmZsb2F0VmFsdWUAE2Zv" +
    "cm1hdFVuc2lnbmVkTG9uZzAACGdldENoYXJzAAdnZXRMb25nAAhoYXNoQ29kZQANaGlnaGVzdE9u" +
    "ZUJpdAAIaW50VmFsdWUACWxvbmdWYWx1ZQAMbG93ZXN0T25lQml0AANtYXgAA21pbgAUbnVtYmVy" +
    "T2ZMZWFkaW5nWmVyb3MAFW51bWJlck9mVHJhaWxpbmdaZXJvcwAJcGFyc2VMb25nABFwYXJzZVVu" +
    "c2lnbmVkTG9uZwARcmVtYWluZGVyVW5zaWduZWQAB3JldmVyc2UADHJldmVyc2VCeXRlcwAKcm90" +
    "YXRlTGVmdAALcm90YXRlUmlnaHQAEHNlcmlhbFZlcnNpb25VSUQACnNob3J0VmFsdWUABnNpZ251" +
    "bQAKc3RyaW5nU2l6ZQADc3VtAA50b0JpbmFyeVN0cmluZwALdG9IZXhTdHJpbmcADXRvT2N0YWxT" +
    "dHJpbmcACHRvU3RyaW5nABR0b1Vuc2lnbmVkQmlnSW50ZWdlcgAQdG9VbnNpZ25lZFN0cmluZwAR" +
    "dG9VbnNpZ25lZFN0cmluZzAABXZhbHVlAAd2YWx1ZU9mAFl+fkQ4eyJjb21waWxhdGlvbi1tb2Rl" +
    "IjoicmVsZWFzZSIsImhhcy1jaGVja3N1bXMiOmZhbHNlLCJtaW4tYXBpIjoxLCJ2ZXJzaW9uIjoi" +
    "Mi4xLjctcjEifQACBgFkHAEYDQIFAWQcAxcfFyMXAgIFAWQcBBckFyEXIxcCBgEvCwAZARkBGQEZ" +
    "ARkBGgYSAYiABMQZAYGABIQaAYGABNgZAQnYDQIJ7A0DCcwOAQn0EAEJrBUECKAaAQjsDgEIjA8B" +
    "CZQRAQnUEQEJtBECCcwPAQnMFQMJ/BUBCZQWAQm0FgEJjBABCaQQAQnUFgEJ9BYBCZQXAQm0FwEJ" +
    "1BcBCfQXAQmUGAEJtBgBCcgYAQngGAEJ9BgCCcAQAQjUEAEJkBkBCdASAQnwEgEJkBMCCewTAQmM" +
    "FAEKjBUBCawUAQnMFAEI7BQBCbQSAQn0EQEJlBIFAfgMAgGMDgHBIKwOBAGYDQEB2AwBAbgNBwGs" +
    "DwMB7A8BAeQVEQGkGQcBsBMEBAgGAAYABEAAAAAAAQAAANwTAAABAAAA5BMAAAEAAADwEwAAHBUA" +
    "AAEAAAAKAAAAAAAAAAQAAAAUFQAAAwAAAAwVAAAKAAAADBUAAB8AAAAMFQAAIAAAAAwVAAAhAAAA" +
    "DBUAACIAAAAMFQAAIwAAAAwVAAAkAAAADBUAADkAAAAMFQAAOgAAAAwVAAARAAAAAAAAAAEAAAAA" +
    "AAAAAQAAAGcAAABwAAAAAgAAABcAAAAMAgAAAwAAACIAAABoAgAABAAAAAcAAAAABAAABQAAAEAA" +
    "AAA4BAAABgAAAAEAAAA4BgAAASAAADoAAABYBgAAAyAAAC4AAABADQAAARAAAA8AAABYDgAAAiAA" +
    "AGcAAADiDgAABCAAAAMAAADcEwAAACAAAAEAAAD+EwAABSAAAAEAAAD/FAAAAxAAAAQAAAAIFQAA" +
    "BiAAAAEAAAAkFQAAABAAAAEAAACMFQAA"
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
