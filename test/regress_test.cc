#include <stdio.h>
#include <iostream>
#include <string.h>
#include <stdlib.h>
#include "mpi.h"
#include <math.h>
#include "../phastaIO.h"
#include "../rdtsc.h"

/*
 * This program uses serial read to read 1pfpp files,
 * then write out using serial write and parallel write
 * to write out files, and also do a parallel read of
 * those new files as well.
 *
 * This also covers all the user cases for testing mem
 * leaks for the library itself.
 *
 * Sample run arguments:
 * exe [starting_step_number] [num_parts] [num_output_files]
 */

#define ClockFrequency 850000000.0

using namespace std;

int main(int argc, char *argv[]) {

	unsigned long long read_serial_timer[4];
	unsigned long long write_serial_timer[4];
	unsigned long long read_parallel_timer[10];
	unsigned long long write_parallel_timer[10];
	unsigned long long total_time[4];
	float time_span[4];

	read_parallel_timer[8]=rdtsc();

	MPI_Init(&argc,&argv);

	int myrank, numprocs;
	MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

	int lstep = atoi(argv[1]); // time step number (e.g., 100 for restart.100.*)
	int numparts = atoi(argv[2]); // total number of parts

	int i, irstin, irstout, iarray[10], isize, nitems;
	int ithree=3;
	int nchar = 255;
	char inrfname[255], outrfname[255], fprefix[255], fieldtag_s[255], hdata[255], path[255];
	char iotype[16];
	strcpy(iotype,"binary");
	int magic_number = 362436;
	int* mptr = &magic_number;

	double **solution_field;
	int *nshg_s, *nv_s, *sn_s;

	// Calculate number of parts each proc deal with and where it start and end ...
	int nppp = numparts/numprocs;// nppp : Number of parts per proc ...
	int startpart = myrank * nppp +1;// Part id from which I (myrank) start ...
	int endpart = startpart + nppp - 1;// Part id to which I (myrank) end ...

	// Allocate space for each var ...
	nshg_s = new int[nppp];
	nv_s = new int[nppp];
	sn_s = new int[nppp];
	solution_field =  new double*[nppp];

	// Find path of the data which you need ...
	sprintf(fprefix,"./%d-procs_case",numparts);

  /****************************************************
   * start of read using serial-lib
   ***************************************************/
	MPI_Barrier(MPI_COMM_WORLD);

	strcpy(fieldtag_s,"solution");

	// Each proc loops over parts ...
	for( i = 0; i < nppp; i++ )
	{

		bzero((void*)inrfname,nchar);
		// During each loop, find the part file ...
		sprintf(inrfname,"%s/restart.%d.%d",fprefix,lstep,startpart + i);

    // this is how to time openfile ..
		MPI_Barrier(MPI_COMM_WORLD);
		read_serial_timer[0] = rdtsc( );
		openfile_(inrfname, "read", &irstin);
		MPI_Barrier(MPI_COMM_WORLD);
		read_serial_timer[1] = rdtsc( );

		// Read first field data ...
		iarray[0]=-1;
		readheader_(&irstin,fieldtag_s,(void*)iarray,&ithree,"double",iotype);
		nshg_s[i]=iarray[0];
		nv_s[i]=iarray[1];
		sn_s[i]=iarray[2];
		if(iarray[0]==-1)
		{
			printf( " [%d] not found ... exiting\n", myrank );
			exit(1);
		}
		isize=nshg_s[i]*nv_s[i];
		// Allocate the field data array to the right size ...
		solution_field[i] = new double[isize];
		// solution_field[i]=(double *) malloc( sizeof( double ) * isize );
		// Now read the array ...
		readdatablock_(&irstin,fieldtag_s,(void*)solution_field[i],&isize,"double",iotype);

		MPI_Barrier(MPI_COMM_WORLD);
		read_serial_timer[2] = rdtsc( );
		closefile_(&irstin, "read");
		MPI_Barrier(MPI_COMM_WORLD);
		read_serial_timer[3] = rdtsc( );
	}
  if(myrank ==0) cout << "********************\nSerial read finished..\n********************\n\n";

  /****************************************************
   * start of write using serial-lib
   ***************************************************/

	for( i = 0; i < nppp; i++ )
	{
		bzero((void*)outrfname,nchar);
		sprintf( path, "writeOutData", myrank+1 );
		sprintf( outrfname,"%s/Serialdata.%d.%d",path, lstep, myrank+1 );

		MPI_Barrier(MPI_COMM_WORLD);
		write_serial_timer[0] = rdtsc( );
		openfile_(outrfname, "write", &irstout);
		MPI_Barrier(MPI_COMM_WORLD);
		write_serial_timer[1] = rdtsc( );

    // mimic original header string
		writestring_( &irstout,"# PHASTA Input File Version 2.0\n");
		writestring_( &irstout, "# Byte Order Magic Number : 362436 \n");

		bzero( (void*)hdata, 255 );
		sprintf(hdata,"# Output generated by phPost version 2.7:  \n");
		writestring_( &irstout, hdata );

		isize = 1;
		nitems = 1;
		iarray[0] = 1;
		writeheader_( &irstout, "byteorder magic number ",
				(void*)iarray, &nitems, &isize, "integer", iotype );

		writedatablock_( &irstout, "byteorder magic number ",
				(void*)mptr, &nitems, "integer", iotype );

		bzero( (void*)hdata, 255 );
		sprintf(hdata,"number of modes : < 0 > %d\n", nshg_s[i]);
		writestring_( &irstout, hdata );

		bzero( (void*)hdata, 255 );
		sprintf(hdata,"number of variables : < 0 > %d\n", nv_s[i]);
		writestring_( &irstout, hdata );

		isize =  nv_s[i]*nshg_s[i];
		nitems = 3;
		iarray[0] = nshg_s[i];
		iarray[1] = nv_s[i];
		iarray[2] = sn_s[i];

		writeheader_( &irstout, fieldtag_s,
				(void*)iarray, &nitems, &isize, "double", iotype);

		nitems = isize;

		writedatablock_( &irstout, fieldtag_s,
				(void*)(solution_field[i]), &nitems, "double", iotype );

		MPI_Barrier(MPI_COMM_WORLD);
		write_serial_timer[2] = rdtsc( );
		closefile_(&irstout, "write");
		MPI_Barrier(MPI_COMM_WORLD);
		write_serial_timer[3] = rdtsc( );
	}
  if(myrank ==0) cout << "********************\nSerial write finished..\n********************\n\n";

  /****************************************************
   * start of write using parallel-lib
   ***************************************************/

	/* Before writing, set up the following parameters:
   *
   * nfiles : number of files do you want to write to
	 * nppf : number of parts each file has
	 * nfields : number of fields we have (in this example, 2)
	 * GPID : global part ID
   *
	 * Thses are the key part of parallel-lib
   */

	MPI_Barrier(MPI_COMM_WORLD);

	int nfiles = atoi(argv[3]);
	int nppf = numparts/nfiles;
	int nfields = 1, GPID;


	int descriptor, descriptor2;
	char filename[255];
	bzero((void*)filename,255);
	// Procs are evenly and successively distributed to all files
	// Lower-rank file will always have lower-rank procs ...
	sprintf(path, "writeOutData", (int)(myrank/(numprocs/nfiles)));
	sprintf(filename,"%s/regress_test_syncio_data.%d.%d",path, lstep, (int)(myrank/(numprocs/nfiles)));

  // parallel lib need to call init first
	initphmpiio_(&nfields, &nppf, &nfiles,&descriptor, "write");

	MPI_Barrier(MPI_COMM_WORLD);
	write_parallel_timer[0] = rdtsc( );
	openfile_(filename, "write", &descriptor);
	MPI_Barrier(MPI_COMM_WORLD);
	write_parallel_timer[1] = rdtsc( );

	for (  i = 0; i < nppp; i++  )
	{
		GPID = myrank * nppp + i + 1;

		// Write solution field ...
		sprintf(fieldtag_s,"solution@%d",GPID);

		isize=nshg_s[i]*nv_s[i];
		iarray[0] = nshg_s[i];
		iarray[1] = nv_s[i];
		iarray[2] = sn_s[i];

		writeheader_( &descriptor, fieldtag_s,(void*)iarray, &ithree, &isize, "double", iotype);
		MPI_Barrier(MPI_COMM_WORLD);
		write_parallel_timer[4] = rdtsc( );

		writedatablock_( &descriptor, fieldtag_s, (void*)(solution_field[i]), &isize, "double", iotype );

	}
	MPI_Barrier(MPI_COMM_WORLD);
	write_parallel_timer[2] = rdtsc( );
	closefile_(&descriptor, "write");
	MPI_Barrier(MPI_COMM_WORLD);
	write_parallel_timer[3] = rdtsc( );

	finalizephmpiio_(&descriptor);

  if(myrank ==0) cout << "********************\nParallel write finished..\n********************\n\n";

  /****************************************************
   * start of read using parallel-lib
   ***************************************************/
	char filename4[255];
	sprintf(path, "writeOutData", (int)(myrank/(numprocs/nfiles)));
	sprintf(filename4,"%s/regress_test_syncio_data.%d.%d",path, lstep, (int)(myrank/(numprocs/nfiles)));

	double **new_solution_field;
	new_solution_field = (double **)malloc( sizeof(double *) * nppp );
	int new_iarray[3];

  queryphmpiio_(filename4, &nfields, &nppf);

	initphmpiio_(&nfields, &nppf, &nfiles,&descriptor2, "read");

	MPI_Barrier(MPI_COMM_WORLD);
	read_parallel_timer[0] = rdtsc( );
	openfile_(filename4, "read", &descriptor2);
	MPI_Barrier(MPI_COMM_WORLD);
	read_parallel_timer[1] = rdtsc( );

	for( i = 0; i < nppp; i++ )
	{
		// Specify which part you want to read using GPID ...
		GPID = startpart + i;
		sprintf( fieldtag_s, "solution@%d", GPID );

		readheader_( &descriptor2, fieldtag_s, (void*)new_iarray, &ithree, "double", iotype );
		nshg_s[i]=new_iarray[0];
		nv_s[i]=new_iarray[1];
		sn_s[i]=new_iarray[2];
		isize=nshg_s[i]*nv_s[i];
		new_solution_field[i]=new double[isize];

		readdatablock_( &descriptor2, fieldtag_s, (void*)(new_solution_field[i]), &isize, "double", iotype );
	}

	// Specify which part you want to read using GPID ...
	// Always remember part id is bigger than proc id differs by 1 ...
	MPI_Barrier(MPI_COMM_WORLD);
	read_parallel_timer[2] = rdtsc( );
	closefile_(&descriptor2, "read");
	MPI_Barrier(MPI_COMM_WORLD);
	read_parallel_timer[3] = rdtsc( );

	finalizephmpiio_(&descriptor2);

  if(myrank ==0) cout << "********************\nParallel read finished..\n********************\n\n";

  // now let's check the value of newly read data with old posix read data
  for(i = 0; i < nppp; i++) {
    for(int j = 0; j < isize; j++) {
      if((fabs(solution_field[i][j] - new_solution_field[i][j])) > 0.000001) {
        cout << "this new value doesn't look right:\n";
        cout << "old=" << solution_field[i][j] << ", new=" << new_solution_field[i][j] << endl;
        exit(1);
      }
    }
  }

	MPI_Barrier(MPI_COMM_WORLD);
  if(myrank ==0) cout << "********************\nAll field values verified..\n********************\n\n";

  /****************************************************
   * start of 2nd write using parallel-lib
   ***************************************************/
  ///*
  memset(filename4, 255, 0);
	sprintf(filename4,"%s/regress_test_syncio_data_verify.%d.%d",path, lstep, (int)(myrank/(numprocs/nfiles)));

	int des;
	initphmpiio_(&nfields, &nppf, &nfiles,&des, "write");
	openfile_(filename4, "write", &des);
  //*/

	for (  i = 0; i < nppp; i++  )
	{
		GPID = myrank * nppp + i + 1;

		// Write solution field ...
		sprintf(fieldtag_s,"solution@%d",GPID);

		isize=nshg_s[i]*nv_s[i];
		iarray[0] = nshg_s[i];
		iarray[1] = nv_s[i];
		iarray[2] = sn_s[i];

		writeheader_( &des, fieldtag_s,(void*)iarray, &ithree, &isize, "double", iotype);
		writedatablock_( &des, fieldtag_s, (void*)(solution_field[i]), &isize, "double", iotype );

	}
	closefile_(&des, "write");
	finalizephmpiio_(&des);

  if(myrank ==0) cout << "********************\nParallel write verify finished..\n********************\n\n";

	/////////////////////////////
	// Processing and Printing //
	/////////////////////////////

	total_time[0]=read_parallel_timer[3]-read_parallel_timer[0];
	total_time[1]=read_parallel_timer[2]-read_parallel_timer[1];
	time_span[0]=(float)total_time[0]/ClockFrequency;
	time_span[1]=(float)total_time[1]/ClockFrequency;
	if (myrank==0)
	{
		printf("\n");
		printf("Number of files is %d\n",nfiles);
		printf("*****************************\n");
		printf("Parallel-lib Read time with open is:     %f\n",time_span[0]);
		printf("Parallel-lib Read time without open is:  %f\n",time_span[1]);
	}

	total_time[0]=write_parallel_timer[3]-write_parallel_timer[0];
	total_time[1]=write_parallel_timer[2]-write_parallel_timer[1];
	total_time[2]=write_parallel_timer[4]-write_parallel_timer[1];
	total_time[3]=write_parallel_timer[2]-write_parallel_timer[4];

	time_span[0]=(float)total_time[0]/ClockFrequency;
	time_span[1]=(float)total_time[1]/ClockFrequency;
	time_span[2]=(float)total_time[2]/ClockFrequency;
	time_span[3]=(float)total_time[3]/ClockFrequency;
	if (myrank==0)
	{
		printf("Parallel-lib Write time with open is:    %f\n",time_span[0]);
		printf("Parallel-lib Write time without open is: %f\n",time_span[1]);
		printf("writeheader time is:  %f\n",time_span[2]);
		printf("writedata time is:    %f\n",time_span[3]);
	}


	total_time[0]=read_serial_timer[3]-read_serial_timer[0];
	total_time[1]=read_serial_timer[2]-read_serial_timer[1];
	time_span[0]=(float)total_time[0]/ClockFrequency;
	time_span[1]=(float)total_time[1]/ClockFrequency;
	if (myrank==0)
	{
		printf("Serial-lib Read time with open is:       %f\n",time_span[0]);
		printf("Serial-lib Read time without open is:    %f\n",time_span[1]);
	}

	total_time[0]=write_serial_timer[3]-write_serial_timer[0];
	total_time[1]=write_serial_timer[2]-write_serial_timer[1];
	time_span[0]=(float)total_time[0]/ClockFrequency;
	time_span[1]=(float)total_time[1]/ClockFrequency;
	if (myrank==0)
	{
		printf("Serial-lib Write time with open is:      %f\n",time_span[0]);
		printf("Serial-lib Write time without open is:   %f\n",time_span[1]);
	}

	read_parallel_timer[9]=rdtsc();

	total_time[0]=read_parallel_timer[9]-read_parallel_timer[8];
	time_span[0]=(float)total_time[0]/ClockFrequency;

	if (myrank==0)
	{
		printf("Total time is:    %f\n", time_span[0]);
	}

	for( i = 0; i < nppp; i++ )
	{
		free ( new_solution_field[i] );
		free ( solution_field[i] );
	}
	free (  solution_field );
	free (  sn_s );
	free (  nv_s );
	free (  nshg_s );
	free (  new_solution_field );

	if(myrank==0)
		printf("Going to finalize...\n");

	MPI_Finalize();

	return 0;
}
