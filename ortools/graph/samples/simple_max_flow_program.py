#!/usr/bin/env python3
# Copyright 2010-2021 Google LLC
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# [START program]
"""From Taha 'Introduction to Operations Research', example 6.4-2."""
# [START import]
from ortools.graph.python import max_flow

import numpy as np
# [END import]


def main():
    """MaxFlow simple interface example."""
    # [START solver]
    # Instantiate a SimpleMaxFlow solver.
    smf = max_flow.SimpleMaxFlow()
    # [END solver]

    # [START data]
    # Define three parallel arrays: start_nodes, end_nodes, and the capacities
    # between each pair. For instance, the arc from node 0 to node 1 has a
    # capacity of 20.
    start_nodes = np.array([0, 0, 0, 1, 1, 2, 2, 3, 3])
    end_nodes = np.array([1, 2, 3, 2, 4, 3, 4, 2, 4])
    capacities = np.array([20, 30, 10, 40, 30, 10, 20, 5, 20])
    # [END data]

    # [START constraints]
    # Add arcs in bulk. 
    #   note: we could have used add_arc_with_capacity(start, end, capacity)
    smf.add_arcs_with_capacity(start_nodes, end_nodes, capacities)
    # [END constraints]

    # [START solve]
    # Find the maximum flow between node 0 and node 4.
    status = smf.solve(0, 4)
    # [END solve]

    # [START print_solution]
    if status != smf.OPTIMAL:
        print('There was an issue with the max flow input.')
        print(f'Status: {status}')
        exit(1)
    print('Max flow:', smf.optimal_flow())
    print('')
    print('  Arc    Flow / Capacity')
    for i in range(smf.num_arcs()):
        print('%1s -> %1s   %3s  / %3s' %
              (smf.tail(i), smf.head(i), smf.flow(i), smf.capacity(i)))
    print('Source side min-cut:', smf.get_source_side_min_cut())
    print('Sink side min-cut:', smf.get_sink_side_min_cut())
    # [END print_solution]


if __name__ == '__main__':
    main()
# [END program]
