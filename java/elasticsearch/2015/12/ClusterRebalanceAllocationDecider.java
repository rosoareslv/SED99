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

package org.elasticsearch.cluster.routing.allocation.decider;

import org.elasticsearch.cluster.routing.ShardRouting;
import org.elasticsearch.cluster.routing.allocation.RoutingAllocation;
import org.elasticsearch.common.inject.Inject;
import org.elasticsearch.common.settings.ClusterSettings;
import org.elasticsearch.common.settings.Setting;
import org.elasticsearch.common.settings.Settings;

import java.util.Locale;

/**
 * This {@link AllocationDecider} controls re-balancing operations based on the
 * cluster wide active shard state. This decided can not be configured in
 * real-time and should be pre-cluster start via
 * <tt>cluster.routing.allocation.allow_rebalance</tt>. This setting respects the following
 * values:
 * <ul>
 * <li><tt>indices_primaries_active</tt> - Re-balancing is allowed only once all
 * primary shards on all indices are active.</li>
 *
 * <li><tt>indices_all_active</tt> - Re-balancing is allowed only once all
 * shards on all indices are active.</li>
 *
 * <li><tt>always</tt> - Re-balancing is allowed once a shard replication group
 * is active</li>
 * </ul>
 */
public class ClusterRebalanceAllocationDecider extends AllocationDecider {

    public static final String NAME = "cluster_rebalance";
    public static final Setting<ClusterRebalanceType> CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING = new Setting<>("cluster.routing.allocation.allow_rebalance", ClusterRebalanceType.INDICES_ALL_ACTIVE.name().toLowerCase(Locale.ROOT), ClusterRebalanceType::parseString, true, Setting.Scope.CLUSTER);

    /**
     * An enum representation for the configured re-balance type.
     */
    public static enum ClusterRebalanceType {
        /**
         * Re-balancing is allowed once a shard replication group is active
         */
        ALWAYS,
        /**
         * Re-balancing is allowed only once all primary shards on all indices are active.
         */
        INDICES_PRIMARIES_ACTIVE,
        /**
         * Re-balancing is allowed only once all shards on all indices are active.
         */
        INDICES_ALL_ACTIVE;

        public static ClusterRebalanceType parseString(String typeString) {
            if ("always".equalsIgnoreCase(typeString)) {
                return ClusterRebalanceType.ALWAYS;
            } else if ("indices_primaries_active".equalsIgnoreCase(typeString) || "indicesPrimariesActive".equalsIgnoreCase(typeString)) {
                return ClusterRebalanceType.INDICES_PRIMARIES_ACTIVE;
            } else if ("indices_all_active".equalsIgnoreCase(typeString) || "indicesAllActive".equalsIgnoreCase(typeString)) {
                return ClusterRebalanceType.INDICES_ALL_ACTIVE;
            }
            throw new IllegalArgumentException("Illegal value for " + CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING + ": " + typeString);
        }
    }

    private volatile ClusterRebalanceType type;

    @Inject
    public ClusterRebalanceAllocationDecider(Settings settings, ClusterSettings clusterSettings) {
        super(settings);
        try {
            type = CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.get(settings);
        } catch (IllegalStateException e) {
            logger.warn("[{}] has a wrong value {}, defaulting to 'indices_all_active'", CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING, CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING.getRaw(settings));
            type = ClusterRebalanceType.INDICES_ALL_ACTIVE;
        }
        logger.debug("using [{}] with [{}]", CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING, type.toString().toLowerCase(Locale.ROOT));

        clusterSettings.addSettingsUpdateConsumer(CLUSTER_ROUTING_ALLOCATION_ALLOW_REBALANCE_SETTING, this::setType);
    }

    private void setType(ClusterRebalanceType type) {
        this.type = type;
    }

    @Override
    public Decision canRebalance(ShardRouting shardRouting, RoutingAllocation allocation) {
        return canRebalance(allocation);
    }

    @Override
    public Decision canRebalance(RoutingAllocation allocation) {
        if (type == ClusterRebalanceType.INDICES_PRIMARIES_ACTIVE) {
            // check if there are unassigned primaries.
            if ( allocation.routingNodes().hasUnassignedPrimaries() ) {
                return allocation.decision(Decision.NO, NAME, "cluster has unassigned primary shards");
            }
            // check if there are initializing primaries that don't have a relocatingNodeId entry.
            if ( allocation.routingNodes().hasInactivePrimaries() ) {
                return allocation.decision(Decision.NO, NAME, "cluster has inactive primary shards");
            }

            return allocation.decision(Decision.YES, NAME, "all primary shards are active");
        }
        if (type == ClusterRebalanceType.INDICES_ALL_ACTIVE) {
            // check if there are unassigned shards.
            if (allocation.routingNodes().hasUnassignedShards() ) {
                return allocation.decision(Decision.NO, NAME, "cluster has unassigned shards");
            }
            // in case all indices are assigned, are there initializing shards which
            // are not relocating?
            if ( allocation.routingNodes().hasInactiveShards() ) {
                return allocation.decision(Decision.NO, NAME, "cluster has inactive shards");
            }
        }
        // type == Type.ALWAYS
        return allocation.decision(Decision.YES, NAME, "all shards are active");
    }
}
