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
  //     static int formatUnsignedLong(long val, int shift, char[] buf, int offset, int len) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     public static String toUnsignedString(long i) {
  //       throw new Error("Method redefined away!");
  //     }
  //     static void getChars(long i, int index, char[] buf) {
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
    "ZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIGludCBmb3JtYXRVbnNpZ25lZExvbmco" +
    "bG9uZyB2YWwsIGludCBzaGlmdCwgY2hhcltdIGJ1ZiwgaW50IG9mZnNldCwgaW50IGxlbikgewog" +
    "ICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAg" +
    "IHB1YmxpYyBzdGF0aWMgU3RyaW5nIHRvU3RyaW5nKGxvbmcgaSkgewogICAgICB0aHJvdyBuZXcg" +
    "RXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMg" +
    "U3RyaW5nIHRvVW5zaWduZWRTdHJpbmcobG9uZyBpKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIHZvaWQgZ2V0Q2hhcnMo" +
    "bG9uZyBpLCBpbnQgaW5kZXgsIGNoYXJbXSBidWYpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBzdGF0aWMgaW50IHN0cmluZ1NpemUo" +
    "bG9uZyB4KSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIp" +
    "OwogICAgfQogICAgcHVibGljIHN0YXRpYyBsb25nIHBhcnNlTG9uZyhTdHJpbmcgcywgaW50IHJh" +
    "ZGl4KSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhyb3cgbmV3IEVycm9y" +
    "KCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcg" +
    "cGFyc2VMb25nKFN0cmluZyBzKSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAg" +
    "dGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJs" +
    "aWMgc3RhdGljIGxvbmcgcGFyc2VVbnNpZ25lZExvbmcoU3RyaW5nIHMsIGludCByYWRpeCkgdGhy" +
    "b3dzIE51bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9k" +
    "IHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBsb25nIHBhcnNlVW5z" +
    "aWduZWRMb25nKFN0cmluZyBzKSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAg" +
    "dGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJs" +
    "aWMgc3RhdGljIGxvbmcgcGFyc2VMb25nKENoYXJTZXF1ZW5jZSBzLCBpbnQgYmVnaW5JbmRleCwg" +
    "aW50IGVuZEluZGV4LCBpbnQgcmFkaXgpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewog" +
    "ICAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQog" +
    "ICAgcHVibGljIHN0YXRpYyBMb25nIHZhbHVlT2YoU3RyaW5nIHMsIGludCByYWRpeCkgdGhyb3dz" +
    "IE51bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJl" +
    "ZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBMb25nIHZhbHVlT2YoU3Ry" +
    "aW5nIHMpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJy" +
    "b3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9u" +
    "ZyBkZWNvZGUoU3RyaW5nIG5tKSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAg" +
    "dGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwcml2" +
    "YXRlIGZpbmFsIGxvbmcgdmFsdWU7CiAgICBwdWJsaWMgTG9uZyhTdHJpbmcgcykgdGhyb3dzIE51" +
    "bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAgICAgIHRoaXMoMCk7CiAgICAgIHRocm93IG5ldyBFcnJv" +
    "cigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIGJ5dGUgYnl0ZVZh" +
    "bHVlKCkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsK" +
    "ICAgIH0KICAgIHB1YmxpYyBzaG9ydCBzaG9ydFZhbHVlKCkgewogICAgICB0aHJvdyBuZXcgRXJy" +
    "b3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBpbnQgaW50VmFs" +
    "dWUoKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwog" +
    "ICAgfQogICAgcHVibGljIGxvbmcgbG9uZ1ZhbHVlKCkgewogICAgICByZXR1cm4gdmFsdWU7CiAg" +
    "ICB9CiAgICBwdWJsaWMgZmxvYXQgZmxvYXRWYWx1ZSgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9y" +
    "KCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgZG91YmxlIGRvdWJs" +
    "ZVZhbHVlKCkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEi" +
    "KTsKICAgIH0KICAgIHB1YmxpYyBpbnQgaGFzaENvZGUoKSB7CiAgICAgIHRocm93IG5ldyBFcnJv" +
    "cigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBpbnQg" +
    "aGFzaENvZGUobG9uZyB2YWx1ZSkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRl" +
    "ZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBib29sZWFuIGVxdWFscyhPYmplY3Qgb2Jq" +
    "KSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAg" +
    "fQogICAgcHVibGljIHN0YXRpYyBMb25nIGdldExvbmcoU3RyaW5nIG5tKSB7CiAgICAgIHRocm93" +
    "IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0" +
    "YXRpYyBMb25nIGdldExvbmcoU3RyaW5nIG5tLCBsb25nIHZhbCkgewogICAgICB0aHJvdyBuZXcg" +
    "RXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMg" +
    "TG9uZyBnZXRMb25nKFN0cmluZyBubSwgTG9uZyB2YWwpIHsKICAgICAgdGhyb3cgbmV3IEVycm9y" +
    "KCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgaW50IGNvbXBhcmVU" +
    "byhMb25nIGFub3RoZXJMb25nKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVm" +
    "aW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBpbnQgY29tcGFyZShsb25nIHgs" +
    "IGxvbmcgeSkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEi" +
    "KTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgaW50IGNvbXBhcmVVbnNpZ25lZChsb25nIHgsIGxv" +
    "bmcgeSkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsK" +
    "ICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgbG9uZyBkaXZpZGVVbnNpZ25lZChsb25nIGRpdmlkZW5k" +
    "LCBsb25nIGRpdmlzb3IpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVk" +
    "IGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgcmVtYWluZGVyVW5zaWduZWQo" +
    "bG9uZyBkaXZpZGVuZCwgbG9uZyBkaXZpc29yKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0" +
    "aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBmaW5hbCBpbnQg" +
    "U0laRSA9IDY0OwogICAgcHVibGljIHN0YXRpYyBmaW5hbCBpbnQgQllURVMgPSBTSVpFIC8gQnl0" +
    "ZS5TSVpFOwogICAgcHVibGljIHN0YXRpYyBsb25nIG1heChsb25nIGEsIGxvbmcgYikgewogICAg" +
    "ICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1" +
    "YmxpYyBzdGF0aWMgbG9uZyBtaW4obG9uZyBhLCBsb25nIGIpIHsKICAgICAgdGhyb3cgbmV3IEVy" +
    "cm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwcml2YXRlIHN0YXRpYyBm" +
    "aW5hbCBsb25nIHNlcmlhbFZlcnNpb25VSUQgPSAwOwp9Cg==");
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQCa2U8HVIS89OqwQULzY+zxDsonBc8PEnrEFQAAcAAAAHhWNBIAAAAAAAAAAPQUAABm" +
    "AAAAcAAAABYAAAAIAgAAIQAAAGACAAAHAAAA7AMAAD4AAAAkBAAAAQAAABQGAACQDwAANAYAAF4O" +
    "AABoDgAAcA4AAHQOAAB3DgAAfg4AAIEOAACEDgAAhw4AAIsOAACTDgAAmA4AAJwOAACfDgAAow4A" +
    "AKgOAACtDgAAsQ4AALYOAAC9DgAAwA4AAMQOAADIDgAAzQ4AANEOAADWDgAA2w4AAOAOAAD/DgAA" +
    "Gw8AADUPAABIDwAAWw8AAHMPAACLDwAAng8AALAPAADEDwAA5w8AAPsPAAAPEAAAKhAAAEIQAABN" +
    "EAAAWBAAAGMQAAB7EAAAnxAAAKIQAACoEAAArhAAALEQAAC1EAAAuxAAAL8QAADCEAAAxhAAAMoQ" +
    "AADSEAAA3BAAAOcQAADwEAAA+xAAAAwRAAAUEQAAJBEAADERAAA5EQAARREAAFkRAABjEQAAbBEA" +
    "AHYRAACFEQAAjxEAAJoRAACoEQAArREAALIRAADIEQAA3xEAAOoRAAD9EQAAEBIAABkSAAAnEgAA" +
    "MxIAAEASAABSEgAAXhIAAGYSAAByEgAAdxIAAIcSAACUEgAAoxIAAK0SAADDEgAA1RIAAOgSAADv" +
    "EgAA+BIAAAMAAAAFAAAABgAAAAcAAAAMAAAAGwAAABwAAAAdAAAAHgAAACAAAAAiAAAAIwAAACQA" +
    "AAAlAAAAJgAAACcAAAAoAAAAKQAAAC8AAAAyAAAANgAAADgAAAADAAAAAAAAAAAAAAAFAAAAAQAA" +
    "AAAAAAAGAAAAAgAAAAAAAAAHAAAAAwAAAAAAAAAIAAAAAwAAAOgNAAAJAAAAAwAAAPANAAAKAAAA" +
    "AwAAAAAOAAALAAAAAwAAAAgOAAALAAAAAwAAABAOAAAMAAAABAAAAAAAAAANAAAABAAAAOgNAAAO" +
    "AAAABAAAABgOAAAPAAAABAAAAAAOAAASAAAABAAAACAOAAAQAAAABAAAACwOAAARAAAABAAAADQO" +
    "AAAVAAAACwAAAOgNAAAXAAAACwAAACwOAAAYAAAACwAAADQOAAAZAAAACwAAADwOAAAaAAAACwAA" +
    "AEQOAAATAAAADwAAAAAAAAAVAAAADwAAAOgNAAAWAAAADwAAABgOAAAUAAAAEAAAAEwOAAAXAAAA" +
    "EAAAACwOAAAVAAAAEQAAAOgNAAAvAAAAEgAAAAAAAAAyAAAAEwAAAAAAAAAzAAAAEwAAAOgNAAA0" +
    "AAAAEwAAAFQOAAA1AAAAEwAAACwOAAA3AAAAFAAAABAOAAALAAMABAAAAAsABAArAAAACwAEACwA" +
    "AAALAAMAMAAAAAsACAAxAAAACwAEAFcAAAALAAQAYwAAAAoAHwABAAAACwAcAAAAAAALAB0AAQAA" +
    "AAsAHwABAAAACwAEADoAAAALAAAAOwAAAAsABgA8AAAACwAHAD0AAAALAAgAPQAAAAsABgA+AAAA" +
    "CwARAD8AAAALAAwAQAAAAAsAAQBBAAAACwAgAEIAAAALAAIAQwAAAAsABQBEAAAACwAeAEUAAAAL" +
    "ABEARgAAAAsAEwBGAAAACwAUAEYAAAALAAMARwAAAAsABABHAAAACwAKAEgAAAALAAMASQAAAAsA" +
    "CQBKAAAACwAKAEsAAAALAAwATAAAAAsADABNAAAACwAEAE4AAAALAAQATwAAAAsADQBQAAAACwAO" +
    "AFAAAAALAA8AUAAAAAsADgBRAAAACwAPAFEAAAALAAwAUgAAAAsACgBTAAAACwAKAFQAAAALAAsA" +
    "VQAAAAsACwBWAAAACwAbAFgAAAALAAQAWQAAAAsABABaAAAACwAMAFsAAAALABYAXAAAAAsAFgBd" +
    "AAAACwAWAF4AAAALABUAXwAAAAsAFgBfAAAACwAXAF8AAAALABoAYAAAAAsAFgBhAAAACwAXAGEA" +
    "AAALABcAYgAAAAsAEABkAAAACwARAGQAAAALABIAZAAAAAwAHAABAAAAEAAcAAEAAAAQABgAOQAA" +
    "ABAAGQA5AAAAEAAVAF8AAAALAAAAEQAAAAwAAADgDQAAKgAAAJQUAAB1EwAAbhQAAAMAAgACAAAA" +
    "vQ0AAAgAAAAiAgoAGgAtAHAgAAACACcCAwABAAIAAACoDQAACAAAACIACgAaAS0AcCAAABAAJwAD" +
    "AAEAAgAAALgNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAMAAQACAAAAww0AAAgAAAAiAAoAGgEtAHAg" +
    "AAAQACcAAgACAAAAAAAAAAAAAgAAABJQDwAEAAQAAgAAAOkMAAAIAAAAIgAKABoBLQBwIAAAEAAn" +
    "AAMAAgACAAAArQ0AAAgAAAAiAgoAGgAtAHAgAAACACcCAgACAAIAAACzDQAABwAAAB8BCwBuIAcA" +
    "EAAKAQ8BAAAEAAQAAgAAAPAMAAAIAAAAIgAKABoBLQBwIAAAEAAnAAYABgACAAAABA0AAAgAAAAi" +
    "AAoAGgEtAHAgAAAQACcAAwABAAIAAADIDQAACAAAACIACgAaAS0AcCAAABAAJwACAAIAAgAAACgN" +
    "AAAIAAAAIgAKABoBLQBwIAAAEAAnAAMAAQACAAAAzQ0AAAgAAAAiAAoAGgEtAHAgAAAQACcAAgAC" +
    "AAAAAAAAAAAAAwAAALsAhAEPAQAABAACAAAAAAAAAAAABgAAABIQpQACAMAChCMPAwIAAgAAAAAA" +
    "AAAAAAIAAAASAA8AAgACAAIAAABhDQAACAAAACIACgAaAS0AcCAAABAAJwACAAEAAgAAAPcMAAAI" +
    "AAAAIgEKABoALQBwIAAAAQAnAQIAAQACAAAAFA0AAAgAAAAiAQoAGgAtAHAgAAABACcBAgACAAIA" +
    "AAAhDQAACAAAACIACgAaAS0AcCAAABAAJwADAAMAAgAAABoNAAAIAAAAIgAKABoBLQBwIAAAEAAn" +
    "AAIAAQACAAAAmw0AAAgAAAAiAQoAGgAtAHAgAAABACcBAgACAAIAAAChDQAACAAAACIACgAaAS0A" +
    "cCAAABAAJwADAAIAAwAAAJYNAAAGAAAAIgALAHAwAgAQAhEAAgACAAIAAABmDQAACAAAACIACgAa" +
    "AS0AcCAAABAAJwACAAIAAgAAAGsNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAIAAgACAAAAcA0AAAgA" +
    "AAAiAAoAGgEtAHAgAAAQACcABAABAAMAAADcDQAAFQAAACIAEABwEDoAAAAaAS4AbiA8ABAAUzEG" +
    "AIYRbjA7ABACbhA9AAAADAARAAAAAgACAAIAAAB1DQAACAAAACIACgAaAS0AcCAAABAAJwADAAMA" +
    "AgAAAHoNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAIAAgACAAAAhQ0AAAgAAAAiAAoAGgEtAHAgAAAQ" +
    "ACcAAwADAAIAAACKDQAACAAAACIACgAaAS0AcCAAABAAJwADAAMAAgAAAJANAAAIAAAAIgAKABoB" +
    "LQBwIAAAEAAnAAIAAgACAAAAgA0AAAgAAAAiAAoAGgEtAHAgAAAQACcABAAEAAIAAAD9DAAACAAA" +
    "ACIACgAaAS0AcCAAABAAJwAEAAIAAAAAAAAAAAAEAAAAFgABALsCEAIDAAEAAAAAANINAAADAAAA" +
    "UyAGABAAAAAEAAIAAAAAAAAAAAAEAAAAFgABALwCEAIEAAQAAgAAAC4NAAAIAAAAIgAKABoBLQBw" +
    "IAAAEAAnAAQABAACAAAANQ0AAAgAAAAiAAoAGgEtAHAgAAAQACcABAAEAAIAAAA8DQAACAAAACIA" +
    "CgAaAS0AcCAAABAAJwACAAEAAgAAAEQNAAAIAAAAIgEKABoALQBwIAAAAQAnAQIAAgACAAAASQ0A" +
    "AAgAAAAiAAoAGgEtAHAgAAAQACcAAgABAAIAAABPDQAACAAAACIBCgAaAC0AcCAAAAEAJwECAAIA" +
    "AgAAAFQNAAAIAAAAIgAKABoBLQBwIAAAEAAnAAQABAACAAAAWg0AAAgAAAAiAAoAGgEtAHAgAAAQ" +
    "ACcAAgACAAAAAAAAAAAAAgAAAH0AEAACAAIAAAAAAAAAAAADAAAAFgAAABAAAAADAAMAAAAAAAAA" +
    "AAABAAAAEAAAAAUAAwAAAAAAAAAAAAUAAAAWAAoAnQICABACAAAEAAQAAAAAAAAAAAACAAAAuyAQ" +
    "AAMAAQACAAAA1w0AAAgAAAAiAAoAGgEtAHAgAAAQACcAAAAAAAAAAAAAAAAAAQAAAA4AAAAEAAIA" +
    "AwAAAOIMAAANAAAAFgAAAHAwAgACASIDCgAaAC0AcCAAAAMAJwMAAAMAAwABAAAA3AwAAAYAAABw" +
    "EDkAAABaAQYADgAEAAQAAgAAAA0NAAAIAAAAIgAKABoBLQBwIAAAEAAnABwBAA48AIwBAQAsPAC3" +
    "AQIAAA4AugECAAAOAIgBAQAOAL0BAgAADgBkBQAAAAAADgBtAwAAAA4AqwEBAA4ArgECAAAOALEB" +
    "AgAADgClAQEADgDFAQIAAA4AyAECAAAOAH8EAAAAAA4AdgEADgBzAgAADgB8AQAOAHkCAAAOAMAB" +
    "AgAADgBwAQAOAF4BAA4AWAEADgBbAQAOAGcBAA4ATwIAAA4AVQEADgBqAQAOAFICAAAOAGECAAAO" +
    "ACEBAA4AhQEBAA4AggECAAAOAJABAA4AtAEBAA4AEgEADgCfAQAOAKgBAQAOAJwBAA4AogEADgCW" +
    "AQAOAJkBAA4AkwEADgBMAA4AAQAAAAkAAAABAAAABAAAAAUAAAAEAAMAFQADAAMAAAACAAAABAAE" +
    "AAEAAAALAAAAAQAAAA4AAAACAAAABAADAAQAAAAHAAMAAwADAAEAAAAPAAAAAgAAAA8AAwACAAAA" +
    "DwAEAAIAAAAPAAsAAQAAAAEAAAADAAAABAADABUACDxjbGluaXQ+AAY8aW5pdD4AAj47AAFCAAVC" +
    "WVRFUwABRAABRgABSQACSUoABklKSUxJSQADSUpKAAJJTAABSgACSkoAA0pKSQADSkpKAAJKTAAD" +
    "SkxJAAVKTElJSQABTAACTEQAAkxKAANMSkkAAkxMAANMTEkAA0xMSgADTExMAB1MZGFsdmlrL2Fu" +
    "bm90YXRpb24vU2lnbmF0dXJlOwAaTGRhbHZpay9hbm5vdGF0aW9uL1Rocm93czsAGExqYXZhL2xh" +
    "bmcvQ2hhclNlcXVlbmNlOwARTGphdmEvbGFuZy9DbGFzczsAEUxqYXZhL2xhbmcvQ2xhc3M8ABZM" +
    "amF2YS9sYW5nL0NvbXBhcmFibGU7ABZMamF2YS9sYW5nL0NvbXBhcmFibGU8ABFMamF2YS9sYW5n" +
    "L0Vycm9yOwAQTGphdmEvbGFuZy9Mb25nOwASTGphdmEvbGFuZy9OdW1iZXI7ACFMamF2YS9sYW5n" +
    "L051bWJlckZvcm1hdEV4Y2VwdGlvbjsAEkxqYXZhL2xhbmcvT2JqZWN0OwASTGphdmEvbGFuZy9T" +
    "dHJpbmc7ABlMamF2YS9sYW5nL1N0cmluZ0J1aWxkZXI7ABZMamF2YS9tYXRoL0JpZ0ludGVnZXI7" +
    "AAlMb25nLmphdmEACU1BWF9WQUxVRQAJTUlOX1ZBTFVFABZNZXRob2QgcmVkZWZpbmVkIGF3YXkh" +
    "ACJSZWRlZmluZWQgTG9uZyEgdmFsdWUgKGFzIGRvdWJsZSk9AAFTAARTSVpFAARUWVBFAAFWAAJW" +
    "SgAEVkpJTAACVkwAAVoAAlpMAAJbQwAGYXBwZW5kAAhiaXRDb3VudAAJYnl0ZVZhbHVlAAdjb21w" +
    "YXJlAAljb21wYXJlVG8AD2NvbXBhcmVVbnNpZ25lZAAGZGVjb2RlAA5kaXZpZGVVbnNpZ25lZAAL" +
    "ZG91YmxlVmFsdWUABmVxdWFscwAKZmxvYXRWYWx1ZQASZm9ybWF0VW5zaWduZWRMb25nAAhnZXRD" +
    "aGFycwAHZ2V0TG9uZwAIaGFzaENvZGUADWhpZ2hlc3RPbmVCaXQACGludFZhbHVlAAlsb25nVmFs" +
    "dWUADGxvd2VzdE9uZUJpdAADbWF4AANtaW4AFG51bWJlck9mTGVhZGluZ1plcm9zABVudW1iZXJP" +
    "ZlRyYWlsaW5nWmVyb3MACXBhcnNlTG9uZwARcGFyc2VVbnNpZ25lZExvbmcAEXJlbWFpbmRlclVu" +
    "c2lnbmVkAAdyZXZlcnNlAAxyZXZlcnNlQnl0ZXMACnJvdGF0ZUxlZnQAC3JvdGF0ZVJpZ2h0ABBz" +
    "ZXJpYWxWZXJzaW9uVUlEAApzaG9ydFZhbHVlAAZzaWdudW0ACnN0cmluZ1NpemUAA3N1bQAOdG9C" +
    "aW5hcnlTdHJpbmcAC3RvSGV4U3RyaW5nAA10b09jdGFsU3RyaW5nAAh0b1N0cmluZwAUdG9VbnNp" +
    "Z25lZEJpZ0ludGVnZXIAEHRvVW5zaWduZWRTdHJpbmcAEXRvVW5zaWduZWRTdHJpbmcwAAV2YWx1" +
    "ZQAHdmFsdWVPZgBZfn5EOHsiY29tcGlsYXRpb24tbW9kZSI6InJlbGVhc2UiLCJoYXMtY2hlY2tz" +
    "dW1zIjpmYWxzZSwibWluLWFwaSI6MSwidmVyc2lvbiI6IjIuMS43LXIxIn0AAgYBYxwBGA0CBQFj" +
    "HAMXHxcjFwICBQFjHAQXJBchFyMXAgYBLQsAGQEZARkBGQEZARoGEgGIgATgGAGBgASgGQGBgAT0" +
    "GAEJtA0CCcgNAwmoDgEJsBABCegUBAjIDgEIvBkBCdAQAQmQEQEJ8BACCYgPAQmIFQMJuBUBCdAV" +
    "AQnwFQEJyA8BCeAPAQmQFgEJsBYBCdAWAQnwFgEJkBcBCbAXAQnQFwEJ5BcBCfwXAQmQGAIJ/A8B" +
    "CJAQAQmsGAEJjBIBCawSAQnMEgIJqBMBCcgTAQrIFAEJ6BMBCYgUAQioFAEJ8BEBCbARAQnQEQUB" +
    "1AwCAegNAcEgiA4EAfQMAQG0DAEBlA0GAegOAwGoDwEBoBUQAcAYBwHsEgQECAYABgAEQAAAAAAA" +
    "AQAAAFMTAAABAAAAWxMAAAEAAABnEwAAjBQAAAEAAAAJAAAAAAAAAAQAAACEFAAAAwAAAHwUAAAK" +
    "AAAAfBQAAB4AAAB8FAAAHwAAAHwUAAAgAAAAfBQAACEAAAB8FAAAIgAAAHwUAAA3AAAAfBQAADgA" +
    "AAB8FAAAEQAAAAAAAAABAAAAAAAAAAEAAABmAAAAcAAAAAIAAAAWAAAACAIAAAMAAAAhAAAAYAIA" +
    "AAQAAAAHAAAA7AMAAAUAAAA+AAAAJAQAAAYAAAABAAAAFAYAAAEgAAA4AAAANAYAAAMgAAAsAAAA" +
    "3AwAAAEQAAAOAAAA4A0AAAIgAABmAAAAXg4AAAQgAAADAAAAUxMAAAAgAAABAAAAdRMAAAUgAAAB" +
    "AAAAbhQAAAMQAAAEAAAAeBQAAAYgAAABAAAAlBQAAAAQAAABAAAA9BQAAA=="
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
