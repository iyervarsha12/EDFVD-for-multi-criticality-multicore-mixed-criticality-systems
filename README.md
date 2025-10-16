# EDFVD-for-multi-criticality-multicore-mixed-criticality-systems
Done while researching under Dr. Raveendran in BITS Goa.
This has the implementation for EDF-VD for multi criticality systems (levels &lt;=14) with multiple cores simulated by threads, and are implicit tasks.
In the event of criticality changes for a core i.e if we go up criticality levels, the lower criticality jobs than that level are discarded, but we save the discarded jobs (and also keep a track of lower criticality jobs coming in) and try to fit it into one of the cores.

Assumptions:
1. It is an implicit taskset, i.e deadline = period.
2. It is a multi criticality system with levels <= 14.
3. There are multiple cores. This implementation assumes 4 cores.
4. If one core changes its criticality level, the others change it too as soon as they can.
5. In the event of trying to fit a discarded job, we first try to fit it in the core the job is statically allocated to, then to the core from which discarded accomodation was called, and finally try the rest of the cores.

Algorithm:

1. Take input parallely using four threads from four different input files, each file having taskset for each core
2. Fill up tasks array using the tasks of each core.
3. Find out hyperperiod as the hyperperiod of all tasks of all cores
4. Parallely do offline preprocessing of the tasks of each core, find x-factor and change virtual deadlines of tasks accordingly. STORE k of each core
5. Main Runtime algorithm:
a. Find all jobs arriving at time=0 in each core parallely, store in to_be_added[core_no]. Now, start the timer to run till hyperperiod.   
b. The decision points are criticality change flag (if other cores are forcing this core to change criticality), next closest arrival times, next closest departure time, and if it's time for a criticality change.   
c. For each core, if it's time for criticality change flag set, increase the criticality level of the core, cause criticality change, and update next closest departure time.
d. For each core, if it's time for next closest arrival, populate next jobs to_be_added[core_no], update next closest departure if any change, and find next closest arrival time again
e. For each core, if it's time for next closest departure time, cause job to depart, update next closest departure time, try to accomodate a discarded jobf. For each core, if it's time for criticality change (i.e core itself had a lower criticality job which overran), increase system level for core, cause criticality change and update next closest departure time
g. Print running job for the core, if any.
h. Increase time by one time gran.

5.1. Criticality change algorithm:
a, if system level is (1...k)(system level[core]<=k) discard tasks of criticality <system level(from ready queue, and no more such arrivals either), others scheduled according to virtual deadlines 
b. if system level is (k+1..no_levels)(system level[core]>k), tasks having criticality <=k discarded, original deadlines of tasks with criticality >k restored (change in ready queue, as well as modify tasks and check arrivals)
c. keep track of highest core criticality. set criticality change flag.
d. try to accomodate discarded job in the core that changed criticality 

5.2. Discarded job accomodation algorithm:
a. discarded job accomodation is called either by a core which has changed criticality, or by a core which just had a job departure.
b. While the global discarded queue is not empty, choose discarded job as the one with the highest criticality (tiebreak with lowest deadline) to try to fit in.
c. If you can fit it in statically allocated core, add it to ready queue of core, and pop it from discarded queue and continue the loop.
d. If not, try to fit it in core which called the accomodation (due to a job departure etc). If can fit in, add it to ready queue of core, and pop it from discarded queue and continue the loop.
e. If not, try the remaining cores. If any core can fit it, add it to ready queue of the core, pop it from the discarded queue, continue the while loop. ELSE:
e.1 if all of the cores have slack <= 0 for the discarded job, then it cannot be accomodated in the future
e.2 If all cores don't have slack =0 for the discarded job, then it may be able to be accomodated in future. Add it to a temporary queue, pop from discarded queue, and continue the while loop.
f. While loop from b is done. Add all jobs from temporary queue to the discarded queue for trying for future fitting in.
