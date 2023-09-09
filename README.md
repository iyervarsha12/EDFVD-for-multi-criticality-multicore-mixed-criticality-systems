# EDFVD-for-multi-criticality-multicore-mixed-criticality-systems
This has the implementation for EDF-VD for multi criticality systems (levels &lt;=14) with multiple cores simulated by threads, and are implicit tasks.
In the event of criticality changes for a core i.e if we go up criticality levels, the lower criticality jobs than that level are discarded, but we save the discarded jobs (and also keep a track of lower criticality jobs coming in) and try to fit it into one of the cores.

Assumptions:
1. It is an implicit taskset, i.e deadline = period.
2. It is a multi criticality system with levels <= 14.
3. There are multiple cores. This implementation assumes 4 cores.
4. If one core changes its criticality level, the others change it too as soon as they can.
5. In the event of trying to fit a discarded job, we first try to fit it in the core the job is statically allocated to, then to the core from which discarded accomodation was called, and finally try the rest of the cores.

Algorithm (also present in a less codey manner in the report):

1. Take input parallely using four threads from four different input files, each file having taskset for each core
2. Fill up tasks array using the tasks of each core.
3. Find out hyperperiod as the hyperperiod of all tasks of all cores
4. Parallely do offline preprocessing of the tasks of each core, find x-factor and change virtual deadlines of tasks accordingly. STORE k of each core

5. Runtime:

a. Find all jobs arriving at time=0 in each core parallely, store in to_be_added[core_no]
b. Calculate next closest departure, criticality change time
while(time<hyperperiod):
{
	parallely for each core, in one time gran:
		if it is time for a decision point:
			if criticality_change flag is set and core criticality <highest criticality, increase system level[core], cause criticality change. update next closest departure time
			if time => next closest arrival time, populate next jobs to_be_added[core_no], update next closest departure if any change, and find next closest arrival time again
			if time => next closest departure time, cause job to depart, update next closest departure time, try to accomodate a discarded job
			if time => criticality change time, increase system level[core], cause criticality change. update next closest departure time
			
		print running job
	increase time by one time granularity
}

c. criticality change algorithm:
if system level is (1...k)(system level[core]<=k), discard tasks of criticality <system level(from ready queue, and no more such arrivals either), others scheduled according to virtual deadlines 
if system level is (k+1..no_levels)(system level[core]>k), tasks having criticality <=k discarded, original deadlines of tasks with criticality >k restored (change in ready queue, as well as modify tasks and check arrivals)
keep track of highest core criticality. set criticality change flag.
try to accomodate discarded job in the core that changed criticality 

d. Discarded job accomodation algorithm:
discarded job accomodation is called either by a core which has changed criticality, or by a core which just had a job departure.
temporary queue is empty.
while the global discarded queue is not empty:
{
	choose discarded job as the one with the highest criticality (and the lowest deadline)
	try to fit it in preference core(core it came from).
	-- if yes, add it to the ready queue of the core, pop it from the discarded queue. Continue the while loop.
	-- if no, try to fit it in the core which called the accomodation
			-- if you can fit it, add it to the ready queue of the core, pop it from discarded queue, continue the while loop.
			-- if no, try the remaining cores. 
				-- if any core could fit it, add it to ready queue of the core, pop it from the discarded queue, continue the while loop.
				-- else:
					-- If all of the cores have slack <= 0 for the discarded job, then it cannot be accomodated in the future - pop it from the discarded queue, and continue the while loop.
					-- If all cores don't have slack =0 for the discarded job, then it may be able to be accomodated in future. Add it to a temporary queue, pop from discarded queue, and continue the while loop.
}
Add all jobs from temporary queue to the discarded queue.
