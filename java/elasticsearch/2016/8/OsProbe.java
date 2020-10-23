/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.elasticsearch.monitor.os;

import org.apache.lucene.util.Constants;
import org.elasticsearch.common.SuppressForbidden;
import org.elasticsearch.common.io.PathUtils;
import org.elasticsearch.monitor.Probes;

import java.io.IOException;
import java.lang.management.ManagementFactory;
import java.lang.management.OperatingSystemMXBean;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.util.List;

public class OsProbe {

    private static final OperatingSystemMXBean osMxBean = ManagementFactory.getOperatingSystemMXBean();

    private static final Method getFreePhysicalMemorySize;
    private static final Method getTotalPhysicalMemorySize;
    private static final Method getFreeSwapSpaceSize;
    private static final Method getTotalSwapSpaceSize;
    private static final Method getSystemLoadAverage;
    private static final Method getSystemCpuLoad;

    static {
        getFreePhysicalMemorySize = getMethod("getFreePhysicalMemorySize");
        getTotalPhysicalMemorySize = getMethod("getTotalPhysicalMemorySize");
        getFreeSwapSpaceSize = getMethod("getFreeSwapSpaceSize");
        getTotalSwapSpaceSize = getMethod("getTotalSwapSpaceSize");
        getSystemLoadAverage = getMethod("getSystemLoadAverage");
        getSystemCpuLoad = getMethod("getSystemCpuLoad");
    }

    /**
     * Returns the amount of free physical memory in bytes.
     */
    public long getFreePhysicalMemorySize() {
        if (getFreePhysicalMemorySize == null) {
            return -1;
        }
        try {
            return (long) getFreePhysicalMemorySize.invoke(osMxBean);
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Returns the total amount of physical memory in bytes.
     */
    public long getTotalPhysicalMemorySize() {
        if (getTotalPhysicalMemorySize == null) {
            return -1;
        }
        try {
            return (long) getTotalPhysicalMemorySize.invoke(osMxBean);
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Returns the amount of free swap space in bytes.
     */
    public long getFreeSwapSpaceSize() {
        if (getFreeSwapSpaceSize == null) {
            return -1;
        }
        try {
            return (long) getFreeSwapSpaceSize.invoke(osMxBean);
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Returns the total amount of swap space in bytes.
     */
    public long getTotalSwapSpaceSize() {
        if (getTotalSwapSpaceSize == null) {
            return -1;
        }
        try {
            return (long) getTotalSwapSpaceSize.invoke(osMxBean);
        } catch (Exception e) {
            return -1;
        }
    }

    /**
     * Returns the system load averages
     */
    public double[] getSystemLoadAverage() {
        if (Constants.LINUX || Constants.FREE_BSD) {
            final String procLoadAvg = Constants.LINUX ? "/proc/loadavg" : "/compat/linux/proc/loadavg";
            double[] loadAverage = readProcLoadavg(procLoadAvg);
            if (loadAverage != null) {
                return loadAverage;
            }
            // fallback
        }
        if (Constants.WINDOWS) {
            return null;
        }
        if (getSystemLoadAverage == null) {
            return null;
        }
        try {
            double oneMinuteLoadAverage = (double) getSystemLoadAverage.invoke(osMxBean);
            return new double[] { oneMinuteLoadAverage >= 0 ? oneMinuteLoadAverage : -1, -1, -1 };
        } catch (Exception e) {
            return null;
        }
    }

    @SuppressForbidden(reason = "access /proc")
    private static double[] readProcLoadavg(String procLoadavg) {
        try {
            List<String> lines = Files.readAllLines(PathUtils.get(procLoadavg));
            if (!lines.isEmpty()) {
                String[] fields = lines.get(0).split("\\s+");
                return new double[] { Double.parseDouble(fields[0]), Double.parseDouble(fields[1]), Double.parseDouble(fields[2]) };
            }
        } catch (IOException e) {
            // do not fail Elasticsearch if something unexpected
            // happens here
        }
        return null;
    }

    public short getSystemCpuPercent() {
        return Probes.getLoadAndScaleToPercent(getSystemCpuLoad, osMxBean);
    }

    private static class OsProbeHolder {
        private static final OsProbe INSTANCE = new OsProbe();
    }

    public static OsProbe getInstance() {
        return OsProbeHolder.INSTANCE;
    }

    private OsProbe() {
    }

    public OsInfo osInfo() {
        OsInfo info = new OsInfo();
        info.availableProcessors = Runtime.getRuntime().availableProcessors();
        info.name = Constants.OS_NAME;
        info.arch = Constants.OS_ARCH;
        info.version = Constants.OS_VERSION;
        return info;
    }

    public OsStats osStats() {
        OsStats stats = new OsStats();
        stats.timestamp = System.currentTimeMillis();
        stats.cpu = new OsStats.Cpu();
        stats.cpu.percent = getSystemCpuPercent();
        stats.cpu.loadAverage = getSystemLoadAverage();

        OsStats.Mem mem = new OsStats.Mem();
        mem.total = getTotalPhysicalMemorySize();
        mem.free = getFreePhysicalMemorySize();
        stats.mem = mem;

        OsStats.Swap swap = new OsStats.Swap();
        swap.total = getTotalSwapSpaceSize();
        swap.free = getFreeSwapSpaceSize();
        stats.swap = swap;

        return stats;
    }

    /**
     * Returns a given method of the OperatingSystemMXBean,
     * or null if the method is not found or unavailable.
     */
    private static Method getMethod(String methodName) {
        try {
            return Class.forName("com.sun.management.OperatingSystemMXBean").getMethod(methodName);
        } catch (Exception e) {
            // not available
            return null;
        }
    }
}
