#include <iostream>
#include <queue>
#include <vector>
#include <cfloat>
#include <math.h>
#include <thread>
#include <mutex>
#include "EDF_VD_multicore.h"

using namespace std;

/*COMPARATOR DEFINITION*/
bool comp::operator()(job* j1, job* j2)
{
		return (j1->job_virtual_deadline >= j2->job_virtual_deadline);
}

bool comp2::operator()(job* j1, job* j2)
{
	if(j1->job_criticality == j2->job_criticality)
		return (j1->job_virtual_deadline >= j2->job_virtual_deadline);
	
	return (j1->job_criticality < j2->job_criticality);
}


/*GLOBAL VARIABLES USED*/ 

//scheduler related variables
int hyperperiod=0;
double time_system = 0;
bool criticality_change=false;
int max_system_level=0;

//variables of different cores
int number_of_tasks[number_of_cores];
int no_levels[number_of_cores];
int k_value[number_of_cores];
vector <int> system_level(number_of_cores,0);

//variables for gloabl discarded job accomodation. should be comp2 for all.
priority_queue <job*, vector<job*>, comp2> discarded_jobs; 
int discarded_job_inclusion_time[number_of_cores];
priority_queue <job*, vector<job*>, comp2> arrivals_slack_calculation [number_of_cores];

mutex s; //used to make print statements mutually exclusive.
mutex s1; //used to make it such that only one operation can use the discarded job queue at a time.

/*FUNCTION DEFINITIONS*/

int gcd(int a, int b)
{
	/*Utility Function to calculate greatest common divisor. Used in lcm to calculate hyperperiod*/
  if (b==0)
	  return a;
  else
  return gcd(b, a%b);
}

int lcm(vector <int> a)
{
	/*Utility Function to calculate least common multiple. Used in hyperperiod_calc*/
  int res = 1;
  for(int i=0;i<a.size();i++)
    res = res*a[i]/gcd(res, a[i]);
  return res;
}

int hyperperiod_calc(vector <task*> (&t)[number_of_cores]) 
{
	/*Function to calculate hyperperiod of tasks*/
	int count=0;
	int total_number_of_tasks=0; //number_of_tasks[0]+number_of_tasks[1]...
	for(int i=0;i<number_of_cores;i++)
		total_number_of_tasks+=number_of_tasks[i];
	
	vector <int> a(total_number_of_tasks);

	for(int i=0;i<number_of_cores;i++)
		for(int j=0;j<number_of_tasks[i];j++)
			a[count++] = t[i][j]->task_actual_deadline;

	int ht=lcm(a);
	return ht;
}


double ulkcalculator(int l, int k, vector <task*> (&t)[number_of_cores], int thread_no) 
{
	/*Calculates ulk value*/
	double ulk=0.0;
	int i=0;
	
	for(int i=0;i<number_of_tasks[thread_no];i++)
	{
		task* temp = t[thread_no][i];
		if(temp->task_criticality==l)
			ulk = ulk + (double)(temp->task_wcet_levels[k]) / (double)(temp->task_actual_deadline);
		
	}
	return ulk;
}

double ulksummer(int llt, int hlt, int k, vector <task*> (&t)[number_of_cores], int thread_no) 
{
	/*Sums ulk value for l=llt to l=hlt*/
	double ulksum=0.0;

	for(int l=llt;l<=hlt; l++)
		ulksum=ulksum + ulkcalculator(l,k,t,thread_no);

	return ulksum;
}

double ullsummer(int llt, int hlt, vector <task*> (&t)[number_of_cores], int thread_no) 
{
	/*Sums ull value for l=llt to l=hlt*/
	double ullsum=0.0;

	for(int l=llt;l<=hlt; l++)
		ullsum=ullsum + ulkcalculator(l,l,t,thread_no);

	return ullsum;
}

bool schedulable_offline_preprocessing(vector <task*> (&t)[number_of_cores], int thread_no)
{
	/*Returns if given set of tasks is schedulable or not. If it is, it also modifies virtual deadline of the tasks.*/
	double x_factor=1;
	
	if(ullsummer(0,no_levels[thread_no]-1,t,thread_no) <=1.0)
	{	
		for(int i=0;i<number_of_tasks[thread_no];i++)
			t[thread_no][i]->task_virtual_deadline = t[thread_no][i]->task_actual_deadline;
		
		k_value[thread_no]=no_levels[thread_no]-1; //DOUBT
		
		s.lock();
		cout<<"Core "<<thread_no<<": x_factor = 1, k = "<<k_value[thread_no]<<endl;
		s.unlock();
		return true;
	}
	else
	{
		int k=0;
		for(k=0;k<no_levels[thread_no]-1;k++)
		{
			double expr1 = ulksummer(k+1,no_levels[thread_no]-1,k,t,thread_no)/(1-ullsummer(0,k,t,thread_no));
			double expr2=(1-ullsummer(k+1,no_levels[thread_no]-1,t,thread_no))/ullsummer(0,k,t,thread_no);
			if ((1-ullsummer(0,k,t,thread_no) >0) &&(expr1<=expr2))
			{
				x_factor=(double)(expr1+expr2)/(double)2.0;
				
				s.lock();
				cout<<"Core "<<thread_no<<": x-factor = "<<x_factor<<", "<<"k = "<<k<<endl;
				s.unlock();
				k_value[thread_no] = k;
				
				for(int i=0;i<number_of_tasks[thread_no];i++)
				{
					if(t[thread_no][i]->task_criticality<=k) 
						t[thread_no][i]->task_virtual_deadline = (double)(t[thread_no][i]->task_actual_deadline);
					else
						t[thread_no][i]->task_virtual_deadline = x_factor*(double)(t[thread_no][i]->task_actual_deadline);
				}
				return true;
			}
		}
		
	}
	return false;
}

/*

criticality change algorithm:
	if system level is (1...k) //<=k, discard tasks of criticality <system level(from ready queue, and no more such arrivals either), others scheduled according to virtual deadlines 
	if system level is (k+1..no_levels)//>k, tasks having criticality <=k discarded, original deadlines of tasks with criticality >k restored (change in ready queue, as well as modify tasks and check arrivals)
	keep track of highest core criticality. set criticality change flag.
	try to accomodate discarded job in the core that changed criticality 
*/

void criticality_change_function(priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], vector <task*> (&t)[number_of_cores], vector <job*> (&to_be_added)[number_of_cores], int thread_no)  //o(nlogn), n is number of jobs in ready queue
{
	if(system_level[thread_no]<=k_value[thread_no])
	{
		//if system level is (1...k) //<=k, discard tasks of criticality <system level(from ready queue, and no more such arrivals either), others scheduled according to virtual deadlines 
		priority_queue <job*, vector<job*>, comp> changed_queue;
		while(!ready[thread_no].empty())
		{
			job* temp = ready[thread_no].top();
			ready[thread_no].pop();
			if(temp->job_criticality >=system_level[thread_no]) //keep these jobs. Discard the other ones.
			{
				changed_queue.push(temp);
			}
			else //jobs to be discarded
			{
				temp->job_virtual_deadline = temp->job_actual_deadline; //should be done?
				discarded_jobs.push(temp);
			}
		}
		ready[thread_no] = changed_queue;		
		//no more tasks of criticality <system level should arrive.
		vector <job*> temp_to_be_added;
		for(job* temp:to_be_added[thread_no])
		{
			if(temp->job_criticality >=system_level[thread_no]) //keep these jobs. Discard the other ones.
				temp_to_be_added.push_back(temp);
			//else, jobs that are to be discarded arrives.
		}
		to_be_added[thread_no].clear();
		to_be_added[thread_no] = temp_to_be_added;
		
	}
	else
	{
		//if system level is (k+1..no_levels)//>k, tasks having criticality <=k discarded, original deadlines of tasks with criticality >k restored (change in ready queue, as well as modify tasks and check arrivals)
		for(int i=0;i<number_of_tasks[thread_no];i++)
		{
			if(t[thread_no][i]->task_criticality > k_value[thread_no])
				t[thread_no][i]->task_virtual_deadline = t[thread_no][i]->task_actual_deadline;
		}

		priority_queue <job*, vector<job*>, comp> changed_queue;
		while(!ready[thread_no].empty())
		{
			job* temp = ready[thread_no].top();
			ready[thread_no].pop();
			if(temp->job_criticality >k_value[thread_no]) //keep these jobs. Discard the other ones.
			{
				changed_queue.push(temp);
			}
			else //jobs to be discarded
			{
				temp->job_virtual_deadline = temp->job_actual_deadline; //should be done?
				discarded_jobs.push(temp);
			}
		}
		ready[thread_no] = changed_queue;
		// no more tasks of criticality <=k should arrive.
		vector <job*> temp_to_be_added;
		for(auto temp:to_be_added[thread_no])
		{
			if(temp->job_criticality >k_value[thread_no]) //keep these jobs. Discard the other ones.
				temp_to_be_added.push_back(temp);
			//else, jobs that are to be discarded arrives.
		}
		to_be_added[thread_no].clear();
		to_be_added[thread_no] = temp_to_be_added;
	}
}


double calculate_next_arrival(vector <task*> (&t)[number_of_cores], vector <job*> (&to_be_added)[number_of_cores], int thread_no) //after time "time_system".
{
	double min=DBL_MAX;
	for(int i=0;i<number_of_tasks[thread_no];i++)
	{
		double temp = ceil((double)((time_system + time_granularity)) /(double)(t[thread_no][i]->task_actual_deadline)) * (t[thread_no][i]->task_actual_deadline); //next release time_system of job. Starts from instance 1, so need to init instance 0s.
		//if(temp<=min && t[thread_no][i]->task_criticality >=system_level[thread_no])  //then admit the job instance
		if(temp<=min) 
		{
			//if criticality change has not occured, or if system_level is <=k and task criticality is >= system_level, or if system level is >k and task criticality is >k...then admit the job instance of the task.
			if((!criticality_change)||(system_level[thread_no]<=k_value[thread_no] && t[thread_no][i]->task_criticality>=system_level[thread_no])||(system_level[thread_no]>k_value[thread_no] && t[thread_no][i]->task_criticality>k_value[thread_no]))
			{
				//job instance number ceil(time_system+1 / deadline): has release_time no*deadline + release, ei same, actual deadline (no+1)*deadline, virtual deadline no*deadline+vd thing
				job* temp_job = new job;
				temp_job->job_id = t[thread_no][i]->task_id;
				temp_job->job_core = t[thread_no][i]->task_core;
				temp_job->job_release_time = temp;
				temp_job->job_execution_time=t[thread_no][i]->task_execution_time;
				temp_job->job_actual_deadline= (int)temp + t[thread_no][i]->task_actual_deadline; //issue when temp is double?
				temp_job->job_virtual_deadline= (double) temp + t[thread_no][i]->task_virtual_deadline; 
				temp_job->job_execution_time_copy = temp_job->job_execution_time;
				temp_job->job_criticality = t[thread_no][i]->task_criticality;
				temp_job->job_wcet_levels = new int[temp_job->job_criticality+1];
				for(int j=0;j<=temp_job->job_criticality;j++)
					(temp_job->job_wcet_levels)[j] = (t[thread_no][i]->task_wcet_levels)[j];
				
				
				if(temp==min)
					to_be_added[thread_no].push_back(temp_job);
				else
				{
					to_be_added[thread_no].clear();
					to_be_added[thread_no].push_back(temp_job);
				}
				min = temp;
			}
			//else, the job is the discarded job. Can add it to discarded queue here.
		}
	}
	return min;
}

	
void update_arrivals_slack_calculation(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], float &window_low, float &window_high, int thread_no)
{
	//considering jobs that arrive within deadline of job we are trying to accomodate.
	float max = window_high;
	for(int i=0;i<number_of_tasks[thread_no];i++)
	{
		//job instance: ri+k*di, ei, (k+1)*di, (k+1)*di, criticality, wcets
		int k=0;
		if(t[thread_no][i]->task_criticality>=system_level[thread_no])
		{
			while((t[thread_no][i]->task_release_time + k*t[thread_no][i]->task_actual_deadline <window_high))
			{
				if((t[thread_no][i]->task_release_time + k*t[thread_no][i]->task_actual_deadline)>=window_low)
				{
					job* temp_job = new job;
					temp_job->job_id = t[thread_no][i]->task_id;
					temp_job->job_core = t[thread_no][i]->task_core;
					temp_job->job_release_time = (t[thread_no][i]->task_release_time + k*t[thread_no][i]->task_actual_deadline);
					temp_job->job_execution_time=t[thread_no][i]->task_execution_time;
					temp_job->job_actual_deadline= (k+1)* t[thread_no][i]->task_actual_deadline;
					temp_job->job_virtual_deadline= temp_job->job_actual_deadline;
					temp_job->job_execution_time_copy = temp_job->job_execution_time;
					temp_job->job_criticality = t[thread_no][i]->task_criticality;
					temp_job->job_wcet_levels = new int[temp_job->job_criticality+1];
					for(int j=0;j<=temp_job->job_criticality;j++)
						(temp_job->job_wcet_levels)[j] = (t[thread_no][i]->task_wcet_levels)[j];
					
					if(max<temp_job->job_actual_deadline) 
					{
						max=temp_job->job_actual_deadline;
					}
					arrivals_slack_calculation[thread_no].push(temp_job);
				}
				k++;
			}
		}
	}
	
	//considering jobs in the ready queue.
	priority_queue <job*, vector<job*>, comp> ready_temp = ready[thread_no]; 
	while(!ready_temp.empty())
	{
		if(max<ready_temp.top()->job_actual_deadline)
			max = ready_temp.top()->job_actual_deadline;
		ready_temp.pop();
	}
	
	window_high=max;
}

void update_arrivals_slack_calculation2(vector <task*> (&t)[number_of_cores], float &window_low, float &window_high, int thread_no) //in order to consider jobs arriving between end of deadline of acco job and max of deadlines. 
{
	for(int i=0;i<number_of_tasks[thread_no];i++)
	{
		//job instance: ri+k*di, ei, (k+1)*di, (k+1)*di, criticality, wcets
		int k=0;
		if(t[thread_no][i]->task_criticality>=system_level[thread_no])
		{
			while((t[thread_no][i]->task_release_time + k*t[thread_no][i]->task_actual_deadline <window_high))
			{
				if((t[thread_no][i]->task_release_time + k*t[thread_no][i]->task_actual_deadline)>=window_low)
				{
					job* temp_job = new job;
					temp_job->job_id = t[thread_no][i]->task_id;
					temp_job->job_core = t[thread_no][i]->task_core;
					temp_job->job_release_time = (t[thread_no][i]->task_release_time + k*t[thread_no][i]->task_actual_deadline);
					temp_job->job_execution_time=t[thread_no][i]->task_execution_time;
					temp_job->job_actual_deadline= (k+1)* t[thread_no][i]->task_actual_deadline;
					temp_job->job_virtual_deadline= temp_job->job_actual_deadline;
					temp_job->job_execution_time_copy = temp_job->job_execution_time;
					temp_job->job_criticality = t[thread_no][i]->task_criticality;
					temp_job->job_wcet_levels = new int[temp_job->job_criticality+1];
					for(int j=0;j<=temp_job->job_criticality;j++)
						(temp_job->job_wcet_levels)[j] = (t[thread_no][i]->task_wcet_levels)[j];
					
					arrivals_slack_calculation[thread_no].push(temp_job);
				}
				k++;
			}
		}
	}
}

bool try_to_accomodate_in_core(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], int thread_no) //discarded_queue accomodation
{
	/*
	if(discarded_jobs.top()->job_actual_deadline <=time_system || discarded_jobs.top()->job_criticality > system_level[thread_no]) //if deadline of job has already passed, or if job from different core has higher criticality than current core
	{
		return false;
	}
	*/
	float window_low = time_system;
	float window_high = (discarded_jobs.top())->job_actual_deadline; 
	float old_window_high =window_high;
	
	s.lock();
	update_arrivals_slack_calculation(t,ready,window_low, window_high, thread_no); //updates arrival during window. ALSO updates window_high if any arriving job/ready job has greater deadline than window.
	s.unlock();
	s.lock();
	update_arrivals_slack_calculation(t,ready,window_low, window_high, thread_no); //updates arrival during window. ALSO updates window_high if any arriving job/ready job has greater deadline than window.
	s.unlock();
	
	float time_used=0;//time_system used by HI criticality jobs		
	//slack calculation of jobs arriving in window: time_system needed!
	while(!arrivals_slack_calculation[thread_no].empty()) 
	{
		//considering partial wcet if needed
		if((arrivals_slack_calculation[thread_no].top())->job_actual_deadline > old_window_high) //then include partial wcet.
			time_used += (float)(arrivals_slack_calculation[thread_no].top()->job_wcet_levels[system_level[thread_no]]) * ((old_window_high-window_low)/((float)arrivals_slack_calculation[thread_no].top()->job_actual_deadline -window_low));
		else 
			time_used += (arrivals_slack_calculation[thread_no].top())->job_wcet_levels[system_level[thread_no]];
		arrivals_slack_calculation[thread_no].pop();
	}
	priority_queue <job*, vector<job*>, comp> ready_temp = ready[thread_no];
	while(!ready_temp.empty())
	{
		if((ready_temp.top())->job_actual_deadline > old_window_high) //then include partial wcet.
		{
			if(ready_temp.top()->job_criticality < system_level[thread_no]) //some discarded job is in ready queue.
				time_used += (float)(ready_temp.top()->job_wcet_levels[(ready_temp.top())->job_criticality]) * ((old_window_high-window_low)/((float)ready_temp.top()->job_actual_deadline-window_low));
			else
				time_used += (float)(ready_temp.top()->job_wcet_levels[system_level[thread_no]]) * ((old_window_high-window_low)/((float)ready_temp.top()->job_actual_deadline-window_low));
				
		}
		else 
		{
			if(ready_temp.top()->job_criticality < system_level[thread_no]) //some discarded job in ready queue
				time_used += (ready_temp.top())->job_wcet_levels[(ready_temp.top())->job_criticality];
			else 
				time_used += (ready_temp.top())->job_wcet_levels[system_level[thread_no]];
			
		}
		ready_temp.pop();
	}
			
	if(old_window_high!=window_high) //then, need to consider partial wcet of all jobs arriving between old_window_high and window_high.
	{
		s.lock();
		update_arrivals_slack_calculation2(t,old_window_high,window_high,thread_no);
		s.unlock();	
		//slack calculation of arriving jobs between old_window_high and new window_high:
		while(!arrivals_slack_calculation[thread_no].empty()) 
		{
			//considering partial wcet if needed
			if((arrivals_slack_calculation[thread_no].top())->job_actual_deadline > old_window_high) //then include partial wcet.
				time_used += (float)(arrivals_slack_calculation[thread_no].top()->job_wcet_levels[system_level[thread_no]]) * ((old_window_high-window_low)/((float)arrivals_slack_calculation[thread_no].top()->job_actual_deadline-window_low));
			else 
				time_used += (arrivals_slack_calculation[thread_no].top())->job_wcet_levels[system_level[thread_no]];
			
			arrivals_slack_calculation[thread_no].pop();
		}
	}
	
	float slack = (old_window_high-window_low) - time_used;
	
	//cout<<"For job "<<discarded_jobs.top()->job_id<<", Slack = "<<slack<<", job wcet = "<<(discarded_jobs.top())->job_wcet_levels[system_level[thread_no]-1]<<endl;
	if(slack > (discarded_jobs.top())->job_wcet_levels[(discarded_jobs.top())->job_criticality])
	{
		ready[thread_no].push(discarded_jobs.top());
		discarded_job_inclusion_time[thread_no] = window_high;
		discarded_jobs.pop();
		return true;
	}
	else
	{
		return false;
	}
}


bool try_to_accomodate_in_core2(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], int thread_no, priority_queue <job*, vector<job*>, comp2> (&queue_temp), bool &zero_slack) //queue_temp accomodatiob
{
	

	
	float window_low = time_system;
	float window_high = (queue_temp.top())->job_actual_deadline; 
	float old_window_high =window_high;
	
	s.lock();
	update_arrivals_slack_calculation(t,ready,window_low, window_high, thread_no); //updates arrival during window. ALSO updates window_high if any arriving job/ready job has greater deadline than window.
	s.unlock();
	s.lock();
	update_arrivals_slack_calculation(t,ready,window_low, window_high, thread_no); //updates arrival during window. ALSO updates window_high if any arriving job/ready job has greater deadline than window.
	s.unlock();
	
	float time_used=0;//time_system used by HI criticality jobs		
	//slack calculation of jobs arriving in window: time_system needed!
	while(!arrivals_slack_calculation[thread_no].empty()) 
	{
		//considering partial wcet if needed
		if((arrivals_slack_calculation[thread_no].top())->job_actual_deadline > old_window_high) //then include partial wcet.
			time_used += (float)(arrivals_slack_calculation[thread_no].top()->job_wcet_levels[system_level[thread_no]]) * ((old_window_high-window_low)/((float)arrivals_slack_calculation[thread_no].top()->job_actual_deadline -window_low));
		else 
			time_used += (arrivals_slack_calculation[thread_no].top())->job_wcet_levels[system_level[thread_no]];
		arrivals_slack_calculation[thread_no].pop();
	}
	priority_queue <job*, vector<job*>, comp> ready_temp = ready[thread_no];
	while(!ready_temp.empty())
	{
		if((ready_temp.top())->job_actual_deadline > old_window_high) //then include partial wcet.
		{
			if(ready_temp.top()->job_criticality < system_level[thread_no]) //some discarded job is in ready queue.
				time_used += (float)(ready_temp.top()->job_wcet_levels[(ready_temp.top())->job_criticality]) * ((old_window_high-window_low)/((float)ready_temp.top()->job_actual_deadline-window_low));
			else
				time_used += (float)(ready_temp.top()->job_wcet_levels[system_level[thread_no]]) * ((old_window_high-window_low)/((float)ready_temp.top()->job_actual_deadline-window_low));
				
		}
		else 
		{
			if(ready_temp.top()->job_criticality < system_level[thread_no]) //some discarded job in ready queue
				time_used += (ready_temp.top())->job_wcet_levels[(ready_temp.top())->job_criticality];
			else 
				time_used += (ready_temp.top())->job_wcet_levels[system_level[thread_no]];
			
		}
		ready_temp.pop();
	}
			
	if(old_window_high!=window_high) //then, need to consider partial wcet of all jobs arriving between old_window_high and window_high.
	{
		s.lock();
		update_arrivals_slack_calculation2(t,old_window_high,window_high,thread_no);
		s.unlock();	
		//slack calculation of arriving jobs between old_window_high and new window_high:
		while(!arrivals_slack_calculation[thread_no].empty()) 
		{
			//considering partial wcet if needed
			if((arrivals_slack_calculation[thread_no].top())->job_actual_deadline > old_window_high) //then include partial wcet.
				time_used += (float)(arrivals_slack_calculation[thread_no].top()->job_wcet_levels[system_level[thread_no]]) * ((old_window_high-window_low)/((float)arrivals_slack_calculation[thread_no].top()->job_actual_deadline-window_low));
			else 
				time_used += (arrivals_slack_calculation[thread_no].top())->job_wcet_levels[system_level[thread_no]];
			
			arrivals_slack_calculation[thread_no].pop();
		}
	}
	
	float slack = (old_window_high-window_low) - time_used;
	/*
	s.lock();
	cout<<"iFor job "<<queue_temp.top()->job_id<<", Slack = "<<slack<<", job wcet = "<<(queue_temp.top())->job_wcet_levels[system_level[thread_no]-1]<<endl;
	s.unlock();
	*/
	if(slack>0)
		zero_slack=false;
	
	if(slack > (queue_temp.top())->job_wcet_levels[(queue_temp.top())->job_criticality])
	{
		ready[thread_no].push(queue_temp.top());
		discarded_job_inclusion_time[thread_no] = window_high;
		queue_temp.pop();
		return true;
	}
	else
	{
		return false;
	}
}


void discarded_job_accomodation(vector <task*> (&t)[number_of_cores], priority_queue <job*, vector<job*>, comp> (&ready)[number_of_cores], int thread_no)
{
	s1.lock();
	priority_queue <job*, vector<job*>, comp2> queue_temp; 
	
	while(!discarded_jobs.empty()) //should change comparator for this..
	{
		s.lock();
		cout<<"Core "<<thread_no<<" - considering job "<<discarded_jobs.top()->job_id<<" ( core "<<discarded_jobs.top()->job_core<<" ) to be accomodated."<<endl;
		s.unlock();
		
		int discarded_job_id =discarded_jobs.top()->job_id;
		int preference_core = discarded_jobs.top()->job_core;
		
		if(discarded_jobs.top()->job_core!=thread_no)
		{
			//first consider trying to accomodate in the job core mentioned.
			if(try_to_accomodate_in_core(t,ready,preference_core))
			{
				s.lock();
				cout<<"(could fit Job "<<discarded_job_id<<" in its alloted core "<<preference_core<<")"<<endl;
				s.unlock();
				continue; //try accomodating another job
			}
			else 
			{
				s.lock();
				//cout<<"(could not fit Job "<<discarded_job_id<<" in its alloted core "<<preference_core<<")"<<endl;
				s.unlock();
			}
		}
		
		//try thread_no core first. If not fit, try all cores other than thread_no and preference_core
		if(try_to_accomodate_in_core(t,ready,thread_no))
		{
			s.lock();
			cout<<"Core "<<thread_no<<" - ACCOMODATING discarded Job "<<discarded_job_id<<" ( core "<<preference_core<<" )"<<endl;
			s.unlock();
			continue; //try accomodating another job.
		}
		else
		{
			queue_temp.push(discarded_jobs.top()); //the job that couldn't be accomodated in thread_no or preference_core,store it for later to see if it should be discarded absolutely or can be fit into another core.
			s.lock();
			//cout<<"Core "<<thread_no<<" - could not accomodate discarded Job "<<discarded_job_id<<" ( core "<<preference_core<<" )"<<endl;
			s.unlock();
			discarded_jobs.pop(); //time to try another job to accomodate in given core.
		}
	}

	//make zero_slack = false if even one core has some slack.
	while(!queue_temp.empty())
	{
		bool zero_slack = true;
		int discarded_job_id =queue_temp.top()->job_id;
		int preference_core = queue_temp.top()->job_core;

		for(int i=0;i<number_of_cores;i++) //trying all cores.
		{
			if(i!=preference_core && i!=thread_no)
			{
				if(try_to_accomodate_in_core2(t,ready,i,queue_temp, zero_slack)) //try to accomodate queue_temp.top() in core i.
				{
					s.lock();
					cout<<"iCore "<<i<<" - ACCOMODATING discarded Job "<<discarded_job_id<<" ( core "<<preference_core<<" )"<<endl;
					s.unlock();
					goto while_end;
				}	
				else 
				{
					s.lock();
					//cout<<"iCore "<<i<<" - could not accomodate discarded Job "<<discarded_job_id<<" ( core "<<preference_core<<" )"<<endl;
					s.unlock();
				}
				
			}
		}
		
		if(zero_slack) //if all the cores that have tried the job have zero slack for it.
		{
			s.lock();
			cout<<"Removing forever - job "<<discarded_job_id<<" ( core "<<preference_core<<" )"<<endl;
			s.unlock();
			queue_temp.pop();
		}
		else //if even one core has >0 slack
		{
			discarded_jobs.push(queue_temp.top());
			queue_temp.pop();
		}
		
		while_end:
		;
	}
	
	end:
		s1.unlock();
}


double minimum(double x, double y, double z)
{
	double min =x;
	if(y<min) min = y;
	if(z<min) min = z;
	return min;
}

/*
ISSUES TO SEE:
If a job is supposed to depart, but a job gets added at same time instant and it is the job that continues(and gets departed).
Check discarded job accomodation.
Check with smaller, verifiable test cases.
*/

void runtime(vector <task*> (&t)[number_of_cores]) 
{
	//at criticality level 'system_level', all tasks of criticality system_level and above can run.
	thread cores[number_of_cores];
	priority_queue <job*, vector<job*>, comp> ready [number_of_cores]; //minheap ordered by deadline for ready queue
	vector <job*> to_be_added[number_of_cores];
	double next_arrival[number_of_cores];
	double next_departure[number_of_cores];
	double next_criticality_change[number_of_cores];
	double next_decision_point[number_of_cores];
	double prev_start = hyperperiod;
	
	//First occurence of all jobs must be initialised.
	//here release time_system of all jobs = 0 and starting time_system is 0; therefore add all jobs of all criticalities to ready queue(assuming starting from criticality level 0)
	auto first_occurence = [&](int thread_no)
	{
		for(int i=0;i<number_of_tasks[thread_no];i++)
		{
			job* temp_job = new job;
			temp_job->job_id = t[thread_no][i]->task_id;
			temp_job->job_core = t[thread_no][i]->task_core;
			temp_job->job_release_time = 0;
			temp_job->job_execution_time=t[thread_no][i]->task_execution_time;
			temp_job->job_actual_deadline= t[thread_no][i]->task_actual_deadline;
			temp_job->job_virtual_deadline= t[thread_no][i]->task_virtual_deadline;
			temp_job->job_execution_time_copy = temp_job->job_execution_time;
			temp_job->job_criticality = t[thread_no][i]->task_criticality;
			temp_job->job_wcet_levels = new int[temp_job->job_criticality+1];
			for(int j=0;j<=temp_job->job_criticality;j++)
				(temp_job->job_wcet_levels)[j] = (t[thread_no][i]->task_wcet_levels)[j];
			ready[thread_no].push(temp_job);
		}
		next_arrival[thread_no] = calculate_next_arrival(t, to_be_added, thread_no);
		
		
		if(!ready[thread_no].empty())
		{
			next_departure[thread_no] = ready[thread_no].top()->job_execution_time + time_system;
			if((ready[thread_no]).top()->job_criticality>=system_level[thread_no]) //if not discarded job
			{
				if((ready[thread_no]).top()->job_execution_time_copy > (ready[thread_no].top()->job_wcet_levels)[system_level[thread_no]]) //exec time will cross wcet level sometime.
					next_criticality_change[thread_no] = time_system + ((ready[thread_no]).top()->job_wcet_levels)[system_level[thread_no]] -(ready[thread_no].top()->job_execution_time_copy - ready[thread_no].top()->job_execution_time);
				else next_criticality_change[thread_no] = hyperperiod; //will never cross wcet level
			}
			else next_criticality_change[thread_no] = hyperperiod; //discarded job never causes criticality change.
		}
		else 
		{
			next_departure[thread_no] = hyperperiod;
			next_criticality_change[thread_no] = hyperperiod;
		}
		
		prev_start=0;
		/*
		s.lock();
		cout<<"Time "<<time_system<<", core "<<thread_no<<" -- next arrival = "<<next_arrival[thread_no]<<", next departure = "<<next_departure[thread_no]<<", next_criticality_change = "<<next_criticality_change[thread_no]<<endl;
		s.unlock();
		*/
		next_decision_point[thread_no] = minimum(next_arrival[thread_no], next_departure[thread_no], next_criticality_change[thread_no]);
	};
	
	for(int i=0;i<number_of_cores;i++)
		cores[i] = thread(first_occurence,i);
	for(int i=0;i<number_of_cores;i++)
		cores[i].join();
	
	
	//need to stop some jobs from being added after criticality change -- condition?
	while(time_system<hyperperiod)
	{
		cout<<"\nTIME "<<time_system<<": "<<endl;
		
		auto runtime_one_time_granularity = [&](int thread_no)
		{
			if(time_system >= next_decision_point[thread_no])
			{
				if(criticality_change && system_level[thread_no]<max_system_level) //cause forced criticality change.
				{
					s.lock();
					cout<<"Core "<<thread_no<<" will have forced criticality change."<<endl;
					s.unlock();
					next_criticality_change[thread_no] = time_system;
				}
				if(time_system >= next_departure[thread_no])
				{
					prev_start = time_system;
					/*
					s.lock();
					cout<<"Core "<<thread_no<<": job "<<ready[thread_no].top()->job_id<<" departs."<<endl;
					s.unlock();
					*/
					ready[thread_no].pop();
					if(!ready[thread_no].empty())
					{
						next_departure[thread_no] = ready[thread_no].top()->job_execution_time + time_system;
						if(time_system<next_criticality_change[thread_no]) //if it is not time for criticality change,
						{
							if((system_level[thread_no]<no_levels[thread_no]) && ((ready[thread_no]).top()->job_criticality>=system_level[thread_no]) && (ready[thread_no]).top()->job_execution_time_copy>ready[thread_no].top()->job_wcet_levels[system_level[thread_no]]) //if not at max level, not discarded job, and exec time will cross wcet level sometime.
								next_criticality_change[thread_no] = time_system + ((ready[thread_no]).top()->job_wcet_levels)[system_level[thread_no]] -(ready[thread_no].top()->job_execution_time_copy - ready[thread_no].top()->job_execution_time);
							else next_criticality_change[thread_no] = hyperperiod; //will never cross wcet level
						}
					}
					else 
					{
						next_departure[thread_no] = hyperperiod;
						next_criticality_change[thread_no] = hyperperiod;
					}
					
					(ready[thread_no].top())->job_execution_time = (ready[thread_no].top())->job_execution_time - (time_system - prev_start);
					discarded_job_accomodation(t,ready,thread_no); //what if new job is added? need to change job exec time of currently running job first, so that next departure calculations can be correct
				}
				
				if(time_system >= next_arrival[thread_no])
				{
					//need to change job exec time of currently running job first, so that next departure calculations can be correct
					(ready[thread_no].top())->job_execution_time = (ready[thread_no].top())->job_execution_time - (time_system - prev_start);
					s.lock(); //...
					//cout<<"core "<<thread_no<<" - job "<<(ready[thread_no].top())->job_id<<" has execution time "<<(ready[thread_no].top())->job_execution_time<<endl;
					s.unlock();
					prev_start = time_system;
					
					for(auto temp_job: to_be_added[thread_no])
					{
						/*
						s.lock();
						cout<<"Core "<<thread_no<<": job "<<temp_job->job_id<<" being added."<<endl;
						s.unlock();
						*/
						ready[thread_no].push(temp_job);
					}
					to_be_added[thread_no].clear();
					next_arrival[thread_no] = calculate_next_arrival(t, to_be_added, thread_no);
					
					if(time_system < next_departure[thread_no]) //what if it's time for departure now? to avoid lost update on next_departure.
					{
						if(!ready[thread_no].empty()) next_departure[thread_no] = ready[thread_no].top()->job_execution_time + time_system; 
						else next_departure[thread_no] = hyperperiod;
					}
					
					if( time_system <next_criticality_change[thread_no] ) //what if it's time for criticality change now? to avoid lost update on next_criticality_change.
					{
						if((!ready[thread_no].empty()) && (system_level[thread_no]<no_levels[thread_no]) && ((ready[thread_no]).top()->job_criticality >= system_level[thread_no])) //if ready queue is not empty, and it is not discarded job
						{
							if((ready[thread_no]).top()->job_execution_time_copy> (ready[thread_no].top()->job_wcet_levels)[system_level[thread_no]]) //exec time will cross wcet level sometime.
								next_criticality_change[thread_no] = time_system + ((ready[thread_no]).top()->job_wcet_levels)[system_level[thread_no]] -(ready[thread_no].top()->job_execution_time_copy - ready[thread_no].top()->job_execution_time);
							else next_criticality_change[thread_no] = hyperperiod;
						}
						else next_criticality_change[thread_no] = hyperperiod;
					}
					
				}
				
				if(time_system >= next_criticality_change[thread_no])
				{
					//FIX: AFTER CRITICALITY CHANGE, WHAT JOBS CAN JOIN READY QUEUE, AND WHICH JOBS CANNOT
					s.lock();
					cout<<"Core "<<thread_no<<" - Criticality change to level "<<system_level[thread_no]+1<<" at time "<<time_system<<endl;
					s.unlock();
					system_level[thread_no]++;
					if(max_system_level<system_level[thread_no])
						max_system_level = system_level[thread_no];
					
					(ready[thread_no].top())->job_execution_time = (ready[thread_no].top())->job_execution_time - (time_system - prev_start);
					s.lock(); //...
					//cout<<"core "<<thread_no<<" - job "<<(ready[thread_no].top())->job_id<<" has execution time "<<(ready[thread_no].top())->job_execution_time<<endl;
					s.unlock();
					prev_start = time_system;
					
					s1.lock(); //since we cannot add things to the discarded queue if the discarded queue is being processed upon.
					criticality_change_function(ready, t,to_be_added,thread_no);
					criticality_change=true;
					s1.unlock();
					
					(ready[thread_no].top())->job_execution_time = (ready[thread_no].top())->job_execution_time - (time_system - prev_start);
					discarded_job_accomodation(t,ready,thread_no); //what if new job is added? need to change job exec time of currently running job first, so that next departure calculations can be correct
					
					if(!ready[thread_no].empty())
					{
						next_departure[thread_no] = ready[thread_no].top()->job_execution_time + time_system;
						if((system_level[thread_no]<no_levels[thread_no])&&((ready[thread_no]).top()->job_criticality>=system_level[thread_no])) //if it is not at max level, and is not discarded job
						{
							if((ready[thread_no]).top()->job_execution_time_copy> (ready[thread_no].top()->job_wcet_levels)[system_level[thread_no]]) //exec time will cross wcet level sometime.
								next_criticality_change[thread_no] = time_system + ((ready[thread_no]).top()->job_wcet_levels)[system_level[thread_no]] -(ready[thread_no].top()->job_execution_time_copy - ready[thread_no].top()->job_execution_time);
							else next_criticality_change[thread_no] = hyperperiod; //never cross wcet level
						}
						else next_criticality_change[thread_no] = hyperperiod; //discarded job never causes criticality change.
					}
					else 
					{
						next_departure[thread_no] = hyperperiod;
						next_criticality_change[thread_no] = hyperperiod;
					}
				}
				
				next_decision_point[thread_no] = minimum(next_arrival[thread_no], next_departure[thread_no], next_criticality_change[thread_no]);
				/*
				s.lock();
				cout<<"Time "<<time_system<<", core "<<thread_no<<" -- next arrival = "<<next_arrival[thread_no]<<", next departure = "<<next_departure[thread_no]<<", next_criticality_change = "<<next_criticality_change[thread_no]<<endl;
				s.unlock();
				*/
			}
				
			//Printing: which job is running
			if(ready[thread_no].empty())
			{
				s.lock();
				cout<<"CORE "<<thread_no<<"- NULL"<<endl;
				s.unlock();
			}
			else
			{
				s.lock();
				if((ready[thread_no].top())->job_core == thread_no)
					cout<<"Core "<<thread_no<<"- "<<"Job "<<((ready[thread_no]).top())->job_id<<endl;
				else cout<<"Core "<<thread_no<<"- "<<"Job "<<((ready[thread_no]).top())->job_id<<" (discarded job from core "<<((ready[thread_no]).top())->job_core<<")"<<endl;
				s.unlock();
			}
			
		};
		
		for(int i=0;i<number_of_cores;i++)
			cores[i] = thread(runtime_one_time_granularity,i);
		for(int i=0;i<number_of_cores;i++)
			cores[i].join();
		
		time_system+=time_granularity;
	}
	
}

task* task_init(int id, int ri, double ei, int di, int l, vector <int> wc, int thread_no)
{
	task* temp = new task;
	temp->task_id = id;
	temp->task_core = thread_no;
	temp->task_release_time = ri;
	temp->task_execution_time=ei;
	temp->task_actual_deadline = di;
	temp->task_virtual_deadline = di;
	temp->task_criticality = l-1; //VERY important because criticalities start from 0 in code, and from 1 in input!
	
	temp->task_wcet_levels = new int[l];
	for(int i=0;i<l;i++)
	{
		(temp->task_wcet_levels)[i] = wc[i];
	}
	return temp;
}


int main()
{	
	thread cores[number_of_cores];
	vector <task*> t[number_of_cores]; //vector of task structures. t[0], t[1], t[2], t[3].
	
	//PARALLELY TAKING INPUT.
	auto take_inputs = [&](int thread_no) 
	{
		FILE *fp;
		//input files input0, input1, input2, input3.
		if(thread_no==0)
			fp = fopen("C:\\Users/\\Varsha\\Desktop\\Systems\\input0.txt","r"); 
		else if(thread_no==1)
			fp = fopen("C:\\Users/\\Varsha\\Desktop\\Systems\\input1.txt","r"); 
		else if(thread_no==2)
			fp = fopen("C:\\Users/\\Varsha\\Desktop\\Systems\\input2.txt","r"); 
		else if(thread_no==3)
			fp = fopen("C:\\Users/\\Varsha\\Desktop\\Systems\\input3.txt","r"); 
		if(!fp)
		{
			cout<<"Could not open input file for core "<<thread_no<<endl;
			return 0;
		}
		fscanf(fp, "%d ",&no_levels[thread_no]);
		fscanf(fp, "%d",&number_of_tasks[thread_no]);
		if(number_of_tasks[thread_no]==0) return 0;
		t[thread_no].resize(number_of_tasks[thread_no]);
		
		for(int i=0;i<number_of_tasks[thread_no];i++)
		{
			int input_release_time,input_deadline,input_criticality; 
			double input_execution_time;
			fscanf(fp, "%d %lf %d %d ", &input_release_time,&input_execution_time,&input_deadline,&input_criticality);
			
			vector <int> input_wcet(input_criticality);
			for(int j=0;j<input_criticality;j++)
				fscanf(fp,"%d ",&input_wcet[j]);
			
			t[thread_no][i] = task_init(i, input_release_time, input_execution_time, input_deadline, input_criticality, input_wcet, thread_no);	
		}
		fclose(fp);
	};
	
	for(int i=0;i<number_of_cores;i++)
		cores[i] = thread(take_inputs,i);
	for(int i=0;i<number_of_cores;i++)
		cores[i].join();
	
	hyperperiod = hyperperiod_calc(t);
	for(int i=0;i<number_of_cores;i++)
		discarded_job_inclusion_time[number_of_cores] = hyperperiod;
	
	//PARALLELY DOING OFFLINE PREPROCESSING.
	auto do_offline_preprocesssing = [&](int thread_no)
	{
		if(! schedulable_offline_preprocessing(t,thread_no))
		{
			cout<<"Input for thread number "<<thread_no<<": Given system cannot be scheduled.\n"<<endl;
			return 0;
		}
	};
	for(int i=0;i<number_of_cores;i++)
		cores[i] = thread(do_offline_preprocesssing,i);
	for(int i=0;i<number_of_cores;i++)
		cores[i].join();
	
	cout<<"Given system can be scheduled. \n"<<endl;
	
	runtime(t);
	return 0;
}


