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
    "ZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIGludCBmb3JtYXRVbnNpZ25lZExvbmco" +
    "bG9uZyB2YWwsIGludCBzaGlmdCwgY2hhcltdIGJ1ZiwgaW50IG9mZnNldCwgaW50IGxlbikgewog" +
    "ICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAg" +
    "IHB1YmxpYyBzdGF0aWMgU3RyaW5nIHRvU3RyaW5nKGxvbmcgaSkgewogICAgICB0aHJvdyBuZXcg" +
    "RXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMg" +
    "U3RyaW5nIHRvVW5zaWduZWRTdHJpbmcobG9uZyBpKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgc3RhdGljIGludCBnZXRDaGFycyhs" +
    "b25nIGksIGludCBpbmRleCwgYnl0ZVtdIGJ1ZikgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1l" +
    "dGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHN0YXRpYyBpbnQgZ2V0Q2hhcnMobG9u" +
    "ZyBpLCBpbnQgaW5kZXgsIGNoYXJbXSBidWYpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRo" +
    "b2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBzdGF0aWMgaW50IHN0cmluZ1NpemUobG9u" +
    "ZyB4KSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwog" +
    "ICAgfQogICAgcHVibGljIHN0YXRpYyBsb25nIHBhcnNlTG9uZyhTdHJpbmcgcywgaW50IHJhZGl4" +
    "KSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgcGFy" +
    "c2VMb25nKFN0cmluZyBzKSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhy" +
    "b3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMg" +
    "c3RhdGljIGxvbmcgcGFyc2VMb25nKENoYXJTZXF1ZW5jZSBzLCBpbnQgYmVnaW5JbmRleCwgaW50" +
    "IGVuZEluZGV4LCBpbnQgcmFkaXgpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAg" +
    "ICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1" +
    "YmxpYyBzdGF0aWMgbG9uZyBwYXJzZVVuc2lnbmVkTG9uZyhTdHJpbmcgcywgaW50IHJhZGl4KSB0" +
    "aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRo" +
    "b2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgcGFyc2VV" +
    "bnNpZ25lZExvbmcoU3RyaW5nIHMpIHRocm93cyBOdW1iZXJGb3JtYXRFeGNlcHRpb24gewogICAg" +
    "ICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1" +
    "YmxpYyBzdGF0aWMgTG9uZyB2YWx1ZU9mKFN0cmluZyBzLCBpbnQgcmFkaXgpIHRocm93cyBOdW1i" +
    "ZXJGb3JtYXRFeGNlcHRpb24gewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmlu" +
    "ZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgTG9uZyB2YWx1ZU9mKFN0cmluZyBz" +
    "KSB0aHJvd3MgTnVtYmVyRm9ybWF0RXhjZXB0aW9uIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIExvbmcgZGVj" +
    "b2RlKFN0cmluZyBubSkgdGhyb3dzIE51bWJlckZvcm1hdEV4Y2VwdGlvbiB7CiAgICAgIHRocm93" +
    "IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHJpdmF0ZSBm" +
    "aW5hbCBsb25nIHZhbHVlOwogICAgcHVibGljIExvbmcoU3RyaW5nIHMpIHRocm93cyBOdW1iZXJG" +
    "b3JtYXRFeGNlcHRpb24gewogICAgICB0aGlzKDApOwogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1l" +
    "dGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBieXRlIGJ5dGVWYWx1ZSgp" +
    "IHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9" +
    "CiAgICBwdWJsaWMgc2hvcnQgc2hvcnRWYWx1ZSgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJN" +
    "ZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgaW50IGludFZhbHVlKCkg" +
    "ewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0K" +
    "ICAgIHB1YmxpYyBsb25nIGxvbmdWYWx1ZSgpIHsKICAgICAgcmV0dXJuIHZhbHVlOwogICAgfQog" +
    "ICAgcHVibGljIGZsb2F0IGZsb2F0VmFsdWUoKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0" +
    "aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIGRvdWJsZSBkb3VibGVWYWx1" +
    "ZSgpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAg" +
    "ICB9CiAgICBwdWJsaWMgaW50IGhhc2hDb2RlKCkgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1l" +
    "dGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgaW50IGhhc2hD" +
    "b2RlKGxvbmcgdmFsdWUpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVk" +
    "IGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgYm9vbGVhbiBlcXVhbHMoT2JqZWN0IG9iaikgewog" +
    "ICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAg" +
    "IHB1YmxpYyBzdGF0aWMgTG9uZyBnZXRMb25nKFN0cmluZyBubSkgewogICAgICB0aHJvdyBuZXcg" +
    "RXJyb3IoIk1ldGhvZCByZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMg" +
    "TG9uZyBnZXRMb25nKFN0cmluZyBubSwgbG9uZyB2YWwpIHsKICAgICAgdGhyb3cgbmV3IEVycm9y" +
    "KCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMgc3RhdGljIExvbmcg" +
    "Z2V0TG9uZyhTdHJpbmcgbm0sIExvbmcgdmFsKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0" +
    "aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHVibGljIGludCBjb21wYXJlVG8oTG9u" +
    "ZyBhbm90aGVyTG9uZykgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCByZWRlZmluZWQg" +
    "YXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgaW50IGNvbXBhcmUobG9uZyB4LCBsb25n" +
    "IHkpIHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAg" +
    "ICB9CiAgICBwdWJsaWMgc3RhdGljIGludCBjb21wYXJlVW5zaWduZWQobG9uZyB4LCBsb25nIHkp" +
    "IHsKICAgICAgdGhyb3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9" +
    "CiAgICBwdWJsaWMgc3RhdGljIGxvbmcgZGl2aWRlVW5zaWduZWQobG9uZyBkaXZpZGVuZCwgbG9u" +
    "ZyBkaXZpc29yKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigiTWV0aG9kIHJlZGVmaW5lZCBhd2F5" +
    "ISIpOwogICAgfQogICAgcHVibGljIHN0YXRpYyBsb25nIHJlbWFpbmRlclVuc2lnbmVkKGxvbmcg" +
    "ZGl2aWRlbmQsIGxvbmcgZGl2aXNvcikgewogICAgICB0aHJvdyBuZXcgRXJyb3IoIk1ldGhvZCBy" +
    "ZWRlZmluZWQgYXdheSEiKTsKICAgIH0KICAgIHB1YmxpYyBzdGF0aWMgZmluYWwgaW50IFNJWkUg" +
    "PSA2NDsKICAgIHB1YmxpYyBzdGF0aWMgZmluYWwgaW50IEJZVEVTID0gU0laRSAvIEJ5dGUuU0la" +
    "RTsKICAgIHB1YmxpYyBzdGF0aWMgbG9uZyBtYXgobG9uZyBhLCBsb25nIGIpIHsKICAgICAgdGhy" +
    "b3cgbmV3IEVycm9yKCJNZXRob2QgcmVkZWZpbmVkIGF3YXkhIik7CiAgICB9CiAgICBwdWJsaWMg" +
    "c3RhdGljIGxvbmcgbWluKGxvbmcgYSwgbG9uZyBiKSB7CiAgICAgIHRocm93IG5ldyBFcnJvcigi" +
    "TWV0aG9kIHJlZGVmaW5lZCBhd2F5ISIpOwogICAgfQogICAgcHJpdmF0ZSBzdGF0aWMgZmluYWwg" +
    "bG9uZyBzZXJpYWxWZXJzaW9uVUlEID0gMDsKfQo="
     );
  private static final byte[] DEX_BYTES = Base64.getDecoder().decode(
    "ZGV4CjAzNQCs172DhVzfoC7GISlYsB/+UzKRxFVLKFkcFgAAcAAAAHhWNBIAAAAAAAAAAEwVAABn" +
    "AAAAcAAAABcAAAAMAgAAIgAAAGgCAAAHAAAAAAQAAD8AAAA4BAAAAQAAADAGAADMDwAAUAYAAK4O" +
    "AAC4DgAAwA4AAMQOAADHDgAAzg4AANEOAADUDgAA1w4AANsOAADhDgAA6Q4AAO4OAADyDgAA9Q4A" +
    "APkOAAD+DgAAAw8AAAcPAAAMDwAAEw8AABYPAAAaDwAAHg8AACMPAAAnDwAALA8AADEPAAA2DwAA" +
    "VQ8AAHEPAACLDwAAng8AALEPAADJDwAA4Q8AAPQPAAAGEAAAGhAAAD0QAABREAAAZRAAAIAQAACY" +
    "EAAAoxAAAK4QAAC5EAAA0RAAAPUQAAD4EAAA/hAAAAQRAAAHEQAACxEAAA8RAAASEQAAFhEAABoR" +
    "AAAeEQAAJhEAADARAAA7EQAARBEAAE8RAABgEQAAaBEAAHgRAACFEQAAjREAAJkRAACtEQAAtxEA" +
    "AMARAADKEQAA2REAAOMRAADuEQAA/BEAAAESAAAGEgAAHBIAADMSAAA+EgAAURIAAGQSAABtEgAA" +
    "exIAAIcSAACUEgAAphIAALISAAC6EgAAxhIAAMsSAADbEgAA6BIAAPcSAAABEwAAFxMAACkTAAA8" +
    "EwAAQxMAAEwTAAADAAAABQAAAAYAAAAHAAAADQAAABwAAAAdAAAAHgAAAB8AAAAhAAAAIwAAACQA" +
    "AAAlAAAAJgAAACcAAAAoAAAAKQAAACoAAAAwAAAAMwAAADYAAAA4AAAAOQAAAAMAAAAAAAAAAAAA" +
    "AAUAAAABAAAAAAAAAAYAAAACAAAAAAAAAAcAAAADAAAAAAAAAAgAAAADAAAALA4AAAkAAAADAAAA" +
    "NA4AAAkAAAADAAAAQA4AAAoAAAADAAAATA4AAAsAAAADAAAAXA4AAAwAAAADAAAAZA4AAAwAAAAD" +
    "AAAAbA4AAA0AAAAEAAAAAAAAAA4AAAAEAAAALA4AAA8AAAAEAAAAdA4AABAAAAAEAAAAXA4AABMA" +
    "AAAEAAAAfA4AABEAAAAEAAAAiA4AABIAAAAEAAAAkA4AABYAAAALAAAALA4AABgAAAALAAAAiA4A" +
    "ABkAAAALAAAAkA4AABoAAAALAAAAmA4AABsAAAALAAAAoA4AABQAAAAPAAAAAAAAABYAAAAPAAAA" +
    "LA4AABcAAAAPAAAAdA4AABUAAAAQAAAAqA4AABgAAAAQAAAAiA4AABYAAAARAAAALA4AADAAAAAS" +
    "AAAAAAAAADMAAAATAAAAAAAAADQAAAATAAAALA4AADUAAAATAAAAiA4AADcAAAAUAAAAbA4AAAsA" +
    "AwAEAAAACwAEACwAAAALAAQALQAAAAsAAwAxAAAACwAIADIAAAALAAQAWAAAAAsABABkAAAACgAg" +
    "AAEAAAALAB4AAAAAAAsAHwABAAAACwAgAAEAAAALAAQAOwAAAAsAAAA8AAAACwAIAD0AAAALAAkA" +
    "PgAAAAsACgA+AAAACwAIAD8AAAALABMAQAAAAAsADgBBAAAACwABAEIAAAALACEAQwAAAAsAAgBE" +
    "AAAACwAHAEUAAAALAAUARgAAAAsABgBGAAAACwATAEcAAAALABUARwAAAAsAFgBHAAAACwADAEgA" +
    "AAALAAQASAAAAAsADABJAAAACwADAEoAAAALAAsASwAAAAsADABMAAAACwAOAE0AAAALAA4ATgAA" +
    "AAsABABPAAAACwAEAFAAAAALAA8AUQAAAAsAEABRAAAACwARAFEAAAALABAAUgAAAAsAEQBSAAAA" +
    "CwAOAFMAAAALAAwAVAAAAAsADABVAAAACwANAFYAAAALAA0AVwAAAAsAHQBZAAAACwAEAFoAAAAL" +
    "AAQAWwAAAAsADgBcAAAACwAYAF0AAAALABgAXgAAAAsAGABfAAAACwAXAGAAAAALABgAYAAAAAsA" +
    "GQBgAAAACwAcAGEAAAALABgAYgAAAAsAGQBiAAAACwAZAGMAAAALABIAZQAAAAsAEwBlAAAACwAU" +
    "AGUAAAAMAB4AAQAAABAAHgABAAAAEAAaADoAAAAQABsAOgAAABAAFwBgAAAACwAAABEAAAAMAAAA" +
    "JA4AACsAAADsFAAAyRMAAMYUAAADAAIAAgAAAAEOAAAIAAAAIgIKABoALgBwIAAAAgAnAgMAAQAC" +
    "AAAA7A0AAAgAAAAiAAoAGgEuAHAgAAAQACcAAwABAAIAAAD8DQAACAAAACIACgAaAS4AcCAAABAA" +
    "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" +
    "ALEBAgAADgC0AQIAAA4AqAEBAA4AyAECAAAOAMsBAgAADgB8BAAAAAAOAHkBAA4AdgIAAA4AggEB" +
    "AA4AfwIAAA4AwwECAAAOAHMBAA4AXgEADgBYAQAOAFsBAA4AZwEADgBPAgAADgBVAQAOAGoBAA4A" +
    "UgIAAA4AYQIAAA4AIQEADgCIAQEADgCFAQIAAA4AkwEADgC3AQEADgASAQAOAKIBAA4AqwEBAA4A" +
    "nwEADgClAQAOAJkBAA4AnAEADgCWAQAOAEwADgABAAAACQAAAAEAAAAEAAAAAwAAAAQAAwAVAAAA" +
    "AwAAAAQAAwAWAAAABQAAAAQAAwAWAAMAAwAAAAIAAAAEAAQAAQAAAAsAAAABAAAADgAAAAIAAAAE" +
    "AAMABAAAAAcAAwADAAMAAQAAAA8AAAACAAAADwADAAIAAAAPAAQAAgAAAA8ACwABAAAAAQAIPGNs" +
    "aW5pdD4ABjxpbml0PgACPjsAAUIABUJZVEVTAAFEAAFGAAFJAAJJSgAESUpJTAAGSUpJTElJAANJ" +
    "SkoAAklMAAFKAAJKSgADSkpJAANKSkoAAkpMAANKTEkABUpMSUlJAAFMAAJMRAACTEoAA0xKSQAC" +
    "TEwAA0xMSQADTExKAANMTEwAHUxkYWx2aWsvYW5ub3RhdGlvbi9TaWduYXR1cmU7ABpMZGFsdmlr" +
    "L2Fubm90YXRpb24vVGhyb3dzOwAYTGphdmEvbGFuZy9DaGFyU2VxdWVuY2U7ABFMamF2YS9sYW5n" +
    "L0NsYXNzOwARTGphdmEvbGFuZy9DbGFzczwAFkxqYXZhL2xhbmcvQ29tcGFyYWJsZTsAFkxqYXZh" +
    "L2xhbmcvQ29tcGFyYWJsZTwAEUxqYXZhL2xhbmcvRXJyb3I7ABBMamF2YS9sYW5nL0xvbmc7ABJM" +
    "amF2YS9sYW5nL051bWJlcjsAIUxqYXZhL2xhbmcvTnVtYmVyRm9ybWF0RXhjZXB0aW9uOwASTGph" +
    "dmEvbGFuZy9PYmplY3Q7ABJMamF2YS9sYW5nL1N0cmluZzsAGUxqYXZhL2xhbmcvU3RyaW5nQnVp" +
    "bGRlcjsAFkxqYXZhL21hdGgvQmlnSW50ZWdlcjsACUxvbmcuamF2YQAJTUFYX1ZBTFVFAAlNSU5f" +
    "VkFMVUUAFk1ldGhvZCByZWRlZmluZWQgYXdheSEAIlJlZGVmaW5lZCBMb25nISB2YWx1ZSAoYXMg" +
    "ZG91YmxlKT0AAVMABFNJWkUABFRZUEUAAVYAAlZKAAJWTAABWgACWkwAAltCAAJbQwAGYXBwZW5k" +
    "AAhiaXRDb3VudAAJYnl0ZVZhbHVlAAdjb21wYXJlAAljb21wYXJlVG8AD2NvbXBhcmVVbnNpZ25l" +
    "ZAAGZGVjb2RlAA5kaXZpZGVVbnNpZ25lZAALZG91YmxlVmFsdWUABmVxdWFscwAKZmxvYXRWYWx1" +
    "ZQASZm9ybWF0VW5zaWduZWRMb25nAAhnZXRDaGFycwAHZ2V0TG9uZwAIaGFzaENvZGUADWhpZ2hl" +
    "c3RPbmVCaXQACGludFZhbHVlAAlsb25nVmFsdWUADGxvd2VzdE9uZUJpdAADbWF4AANtaW4AFG51" +
    "bWJlck9mTGVhZGluZ1plcm9zABVudW1iZXJPZlRyYWlsaW5nWmVyb3MACXBhcnNlTG9uZwARcGFy" +
    "c2VVbnNpZ25lZExvbmcAEXJlbWFpbmRlclVuc2lnbmVkAAdyZXZlcnNlAAxyZXZlcnNlQnl0ZXMA" +
    "CnJvdGF0ZUxlZnQAC3JvdGF0ZVJpZ2h0ABBzZXJpYWxWZXJzaW9uVUlEAApzaG9ydFZhbHVlAAZz" +
    "aWdudW0ACnN0cmluZ1NpemUAA3N1bQAOdG9CaW5hcnlTdHJpbmcAC3RvSGV4U3RyaW5nAA10b09j" +
    "dGFsU3RyaW5nAAh0b1N0cmluZwAUdG9VbnNpZ25lZEJpZ0ludGVnZXIAEHRvVW5zaWduZWRTdHJp" +
    "bmcAEXRvVW5zaWduZWRTdHJpbmcwAAV2YWx1ZQAHdmFsdWVPZgBZfn5EOHsiY29tcGlsYXRpb24t" +
    "bW9kZSI6InJlbGVhc2UiLCJoYXMtY2hlY2tzdW1zIjpmYWxzZSwibWluLWFwaSI6MSwidmVyc2lv" +
    "biI6IjIuMS43LXIxIn0AAgYBZBwBGA0CBQFkHAMXIBckFwICBQFkHAQXJRciFyQXAgYBLgsAGQEZ" +
    "ARkBGQEZARoGEgGIgAS8GQGBgAT8GQGBgATQGQEJ0A0CCeQNAwnEDgEJjBEBCcQVBAjkDgEIhA8B" +
    "CKQPAQmsEQEJ7BEBCcwRAgnkDwEJ5BUDCZQWAQmsFgEJzBYBCaQQAQm8EAEJ7BYBCYwXAQmsFwEJ" +
    "zBcBCewXAQmMGAEJrBgBCcAYAQnYGAEJ7BgCCdgQAQjsEAEJiBkBCegSAQmIEwEJqBMCCYQUAQmk" +
    "FAEKpBUBCcQUAQnkFAEIhBUBCcwSAQmMEgEJrBIFAfAMAgGEDgHBIKQOBAGQDQEB0AwBAbANBwHE" +
    "DwMBhBABAfwVEAGcGQcByBMEBAgGAAYABEAAAAAAAAEAAACnEwAAAQAAAK8TAAABAAAAuxMAAOQU" +
    "AAABAAAACQAAAAAAAAAEAAAA3BQAAAMAAADUFAAACgAAANQUAAAfAAAA1BQAACAAAADUFAAAIQAA" +
    "ANQUAAAiAAAA1BQAACMAAADUFAAAOAAAANQUAAA5AAAA1BQAABEAAAAAAAAAAQAAAAAAAAABAAAA" +
    "ZwAAAHAAAAACAAAAFwAAAAwCAAADAAAAIgAAAGgCAAAEAAAABwAAAAAEAAAFAAAAPwAAADgEAAAG" +
    "AAAAAQAAADAGAAABIAAAOQAAAFAGAAADIAAALQAAABgNAAABEAAADwAAACQOAAACIAAAZwAAAK4O" +
    "AAAEIAAAAwAAAKcTAAAAIAAAAQAAAMkTAAAFIAAAAQAAAMYUAAADEAAABAAAANAUAAAGIAAAAQAA" +
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
