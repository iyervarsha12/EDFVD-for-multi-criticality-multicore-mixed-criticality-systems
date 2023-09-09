#ifndef EDF_VD_H
#define EDF_VD_H

#include <queue>
#include <vector>
using namespace std; 

#define time_granularity 1
#define number_of_cores 4

/*INPUT FORMAT
	Criticality level starts from 1, goes till no_levels. 
	Release_time must be 0 in the input, for now. 
	Tasks are ID-d from 0.
	
	no_of_criticality_levels
	number_of_tasks 
	task0: release_time execution_time actual_deadline criticality_level wcet1 wcet2...wcet_criticality_level
	task1: ...
	
	Sample input 1:
	2
	2 
	0 3 10 1 2 
	0 1 10 2 1 9
	
	Sample input 2:
	5
	6
	0 1 18 2 1 1 
	0 1 30 1 2
	0 1 15 3 2 2 3
	0 1 10 5 1 1 1 2 2
	0 1 9 5 1 2 2 3 4
	0 1 45 1 3
*/


/*STRUCTURES AND VARIABLES USED*/

struct task
{
	int task_id;
	int task_core;
	int task_release_time;
	double task_execution_time;  //given execution time
	int task_actual_deadline; 
	double task_virtual_deadline; 
	int task_criticality;
	int* task_wcet_levels; 
}; 

struct job
{
	int job_id;
	int job_core;
	int job_release_time; //absolute release time
	double job_execution_time; //remaining execution time for job 
	int job_actual_deadline; //absolute deadline
	double job_virtual_deadline; //absolute deadline
	int job_criticality;
	int* job_wcet_levels;
	double job_execution_time_copy; //needed for criticality change check
};

struct comp //comparator for minheap
{
	bool operator()(job* j1, job* j2);
};

struct comp2 //comparator comparing criticality level first, then min deadline.
{
	bool operator()(job* j1, job* j2);
};

/*FUNCTION DECLARATIONS*/

//Functions required for preprocessing
int gcd(int a, int b); //utility function for hyperperiod_calc
int lcm(vector <int> a); //utility function for hyperperiod_calc
int hyperperiod_calc(vector <task*> (&t)[number_of_cores]);  //To calculate hyperperiod
double ulkcalculator(int l, int k, vector <task*> (&t)[number_of_cores], int thread_no) ; //utility function for schedulable_offline_preprocessing
double ulksummer(int llt, int hlt, int k, vector <task*> (&t)[number_of_cores], int thread_no) ; //utility function for schedulable_offline_preprocessing
double ullsummer(int llt, int hlt, vector <task*> (&t)[number_of_cores], int thread_no) ; //utility function for schedulable_offline_preprocessing
bool schedulable_offline_preprocessing(vector <task*> (&t)[number_of_cores], int thread_no); //returns if set of tasks is schedulable or not. If true, modifies according to x_factor

//Function required to handle criticality change.
void criticality_change_function(priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], vector <task*> (&t)[number_of_cores],vector <job*> (&to_be_added)[number_of_cores], int thread_no); 
 
//Functions required for discarded job accomodation.
void update_arrivals_slack_calculation(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], float &window_low, float &window_high, int thread_no); //considering jobs arriving between current time and deadline of acco job.
void update_arrivals_slack_calculation2(vector <task*> (&t)[number_of_cores], float &window_low, float &window_high, int thread_no); //in order to consider jobs arriving between end of deadline of acco job and max of deadlines. 
bool try_to_accomodate_in_core(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], int thread_no); //to try to fit in the discarded job in its alloted core.
void discarded_job_accomodation(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], int thread_no); //To try to fit in the discarded job in the given thread.

//Functions required for running the system and taking input.
double calculate_next_arrival(vector <task*> (&t)[number_of_cores], vector <job*> (&to_be_added)[number_of_cores], int thread_no); //Calculates next arrival
double minimum(double x, double y, double z); //to calculate minimum of the three decision points.
void runtime(vector <task*> (&t)[number_of_cores]);
task* task_init(int id, int ri, double ei, int di, int l, vector <int> wc, int thread_no);


#endif