
#ifndef SPAWN_PMI_CLIENT_H_INCLUDED
#define SPAWN_PMI_CLIENT_H_INCLUDED 1

int PMI2_Init(int *spawned, int *size, int *rank, int *appnum, int *version, int *subversion);
int PMI2_Finalize();

int PMI2_KVS_Put(const char *key, const char *value);
int PMI2_KVS_Get(const char *job_id, int src_pmi_id, const char *key, char *value, int maxval, int *vallen);

int PMI2_KVS_Fence();

#endif

