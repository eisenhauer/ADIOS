/*  
 * in the COPYING file in the top level directory of this source distribution.
 *
 * Copyright (c) 2008 - 2009.  UT-BATTELLE, LLC. All rights reserved.
 */

/* ADIOS bp2bp utility 
 *  read all variables and attributes from 
 *    all groups in a BP file and output this to a adios file
 *
 * This is a sequential program.
 */


#ifndef _GNU_SOURCE
#   define _GNU_SOURCE
#endif


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>   // LONG_MAX36

#include <math.h>     // NAN
#include <libgen.h>   // basename
#include <regex.h>    // regular expression matching
#include <fnmatch.h>  // shell pattern matching

#include "mpi.h"
#include "adios_read.h"
#include "adios_types.h"
#include "adios.h"


#ifdef DMALLOC
#include "dmalloc.h"
#endif

typedef int bool;
#define false 0
#define true  1



MPI_Comm   comm = MPI_COMM_WORLD;


//#define MAX_BUFFERSIZE 81 
#define MAX_BUFFERSIZE 10485760
#define MAX_DIMS 20
#define DEBUG 0
#define verbose 1

int getTypeInfo( enum ADIOS_DATATYPES adiosvartype, int* elemsize);


int getTypeInfo( enum ADIOS_DATATYPES adiosvartype, int* elemsize)
{
  switch(adiosvartype) {
  case adios_unsigned_byte:
    *elemsize = 1;
    break;
  case adios_byte:
    *elemsize = 1;
    break;
  case adios_string:
    *elemsize = 1;
    break;

  case adios_unsigned_short:  
    *elemsize = 2;
    break;
  case adios_short:
    *elemsize = 2;
    break;

  case adios_unsigned_integer:
    *elemsize = 4;
    break;
  case adios_integer:    
    *elemsize = 4;
    break;

  case adios_unsigned_long:
    *elemsize = 8;
    break;
  case adios_long:        
    *elemsize = 8;
    break;   

  case adios_real:
    *elemsize = 4;
    break;

  case adios_double:
    *elemsize = 8;
    break;

  case adios_complex:  
    *elemsize = 8;
    break;

  case adios_double_complex:
    *elemsize = 16;
    break;

  case adios_long_double: // do not know how to print
    //*elemsize = 16;
  default:
    return 1;
  }
  return 0;
}

int main (int argc, char ** argv)  
{
    char       filename [256]; 
    char       gbounds[24], lbounds[24], offs[24],tstring[8];
    int        rank, size, gidx, i, j, k,l, ii;
    enum       ADIOS_DATATYPES attr_type;
    void       * data = NULL;
    uint64_t   MAX_BUFFER = 1024 * 1024 * 100;
    uint64_t   start[] = {0,0,0,0,0,0,0,0,0,0};
    uint64_t   s[] = {0,0,0,0,0,0,0,0,0,0};
    uint64_t   c[] = {0,0,0,0,0,0,0,0,0,0};
    uint64_t   count[MAX_DIMS], hcount[MAX_DIMS], bytes_read = 0;
    char       aname[256],fname[256];
    int64_t        dims [MAX_DIMS];
    int        st, ct,out_size;
    char       ** grp_name;
    int64_t    m_adios_group, m_adios_file;
    int64_t        msize;
    uint64_t   adios_groupsize, adios_totalsize;
    int        err;
    int64_t        readsize;
    int        read_planes = 1;
    MPI_Init(&argc,&argv);
    MPI_Comm_rank(comm,&rank);
    MPI_Comm_size(comm,&size);

    if (argc < 2) {
        printf("Usage: %s <BP-file> <HDF5-file>\n", argv[0]);
        return 1;
    }


    ADIOS_FILE * f = adios_fopen (argv[1], comm);
    adios_init_noxml(); // no xml will be used to write the new adios file
    adios_allocate_buffer (ADIOS_BUFFER_ALLOC_NOW, 100); // allocate 100MB buffer

    if (f == NULL) {
        if (DEBUG) printf ("%s\n", adios_errmsg());
	return -1;
    }
    adios_groupsize = 0;
    /* For all groups */
    for (gidx = 0; gidx < f->groups_count; gidx++) {
        if (DEBUG) printf("Group %s:\n", f->group_namelist[gidx]);
        ADIOS_GROUP * g = adios_gopen (f, f->group_namelist[gidx]);

        if (g == NULL) {
            if (DEBUG) printf ("%s\n", adios_errmsg());
            return -1;
        }
	/* First create all of the groups */
	// now I need to create this group in the file that will be written
	adios_declare_group(&m_adios_group,f->group_namelist[gidx],"",adios_flag_yes);
        adios_select_method (m_adios_group, "MPI", "", "");
	
        for (i = 0; i < g->vars_count; i++) {
	  ADIOS_VARINFO * v = adios_inq_var_byid (g, i);
	  // now I can declare the variable...
	  // if it's a scalar then we declare like:
	  if (v->ndim == 0) {
	    adios_define_var(m_adios_group,g->var_namelist[i],"",v->type,0,0,0);
	    err = getTypeInfo( v->type, &out_size);
	    adios_groupsize+= out_size;
	    if (DEBUG) printf("name=%s size=%d\n",g->var_namelist[i],out_size);
	  } else {
	    // we will do some string maniupulation to set the global bounds, offsets, and local bounds... 
	    j = 0 ;
            if (DEBUG) printf("Scott first dim = %d\n",v->dims[0]);
	    for ( ii=0;ii<v->dims[0];ii++ ) { //split the first dimension...
	      strcpy(gbounds,"");
	      strcpy(lbounds,"");
	      strcpy(offs,"");
	      // we divide the first dimension up.. not the last...
	      sprintf(tstring,"%d,",(int)v->dims[0]);
	      strcat(gbounds,tstring);
	      
	      sprintf(tstring,"%d,",(int) 1);
	      strcat(lbounds,tstring);	      
	      
	      sprintf(tstring,"%d,",(int) ii);//the offset is the plane ii
	      strcat(offs,tstring);

	      for (j=1;j<v->ndim-1;j++) {
		sprintf(tstring,"%d,",(int)v->dims[j]);
		strcat(gbounds,tstring);
		
		sprintf(tstring,"%d,",(int)v->dims[j]);
		strcat(lbounds,tstring);
		
		sprintf(tstring,"%d,",(int) 0);
		strcat(offs,tstring);
	      }
	      
	      sprintf(tstring,"%d\0",(int)v->dims[j]);
	      strcat(gbounds,tstring);
	      
	      sprintf(tstring,"%d\0",(int)v->dims[j]);
	      strcat(lbounds,tstring);
	      
	      sprintf(tstring,"%d\0",(int) 0);
	      strcat(offs,tstring);
	      
	      adios_define_var(m_adios_group,g->var_namelist[i],"",v->type,lbounds,gbounds,offs);
	      if (DEBUG) printf("name gbounds=%s: lbounds=%s: offs=%s\n",gbounds, lbounds, offs);
	      // we have a new variable for each sub block being written........
	    }// end of looping for the splitting of the last dimension
	    
	    
	    msize = 1;
	    for (j=0;j<v->ndim;j++) {
	      msize = msize * v->dims[j];
	    }
	    err = getTypeInfo( v->type, &out_size);
	    adios_groupsize+= out_size * msize;
	    if (DEBUG) printf("name=%s size=%d\n",g->var_namelist[i],out_size*msize);
	    
	  }
	} // finished declaring all of the variables
	// Now we can define the attributes....
	
        if (DEBUG) printf("  Attributes=%d:\n", g->attrs_count);
        for (i = 0; i < g->attrs_count; i++) {
	  enum ADIOS_DATATYPES atype;
	  int  asize;
	  void *adata;
	  adios_get_attr_byid (g, i, &atype, &asize, &adata);
	  if (DEBUG) printf("attribute name=%s\n",g->attr_namelist[i]);
	  adios_define_attribute(m_adios_group,g->attr_namelist[i],"",atype,adata,g->attr_namelist[i]);
	}
	/*------------------------------ NOW WE WRITE -------------------------------------------- */
	// Now we have everything declared... now we need to write them out!!!!!!
	
	// open up the file for writing....
	printf("opening file = %s, with group %s, size=%lld\n",argv[2],f->group_namelist[gidx],adios_groupsize);
	adios_open(&m_adios_file, f->group_namelist[gidx],argv[2],"a",&comm);
	adios_group_size( m_adios_file, adios_groupsize, &adios_totalsize);
	// now we have to write out the variables.... since they are all declared now
	// This will be the place we actually write out the data!!!!!!!!
        for (i = 0; i < g->vars_count; i++) {
	  ADIOS_VARINFO * v = adios_inq_var_byid (g, i);
	  // first we can write out the scalars very easily........
	  if (v->ndim == 0) {
	    adios_write(m_adios_file,g->var_namelist[i],v->value); //I think there will be a problem for strings.... Please check this out SAK
	  } else {
	    // now we will read in the variable, and then write it out....
	    // allocate the memory, read in the data, and then write it out....
	    for (ii=0;ii<v->dims[0];ii++) { // loop on the reading of the first dimension....
	      readsize = 1;	      
	      for (j=1;j<(v->ndim);j++) {
		readsize = readsize * v->dims[j];
		s[j] = 0;
		c[j] = v->dims[j];
	      } // we are reading in with a 1 on the last dimension... so this will be the correct size...
	      s[0] = ii; // this is the plane we are reading....
	      c[0] = 1; // we are only reading 1 plane....
	      err = getTypeInfo( v->type, &out_size);
	      data = (void *) malloc(readsize*out_size);
	      bytes_read = adios_read_var_byid(g,v->varid,s,c,data);
	      // ok... now we write this out....
              if (DEBUG) printf ("%s %d\n",g->var_namelist[i],bytes_read);
	      adios_write(m_adios_file,g->var_namelist[i],data);
	      free(data);
	    }
	  }
	}// end of the writing of the variable..
	adios_close(m_adios_file);
	adios_gclose(g);
    } // end of all of the groups
    adios_fclose(f);
    adios_finalize(rank);
    return(0);
}
