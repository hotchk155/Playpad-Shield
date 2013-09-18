//////////////////////////////////////////////////////
typedef unsigned int FIFO_ENTRY;

typedef struct 
{
	int size;
	int head;
	int tail;
	vos_mutex_t mAccess;	
	FIFO_ENTRY *pdata;
} SynchFIFO;
	
void fifo_init(SynchFIFO *p, int size);
void fifo_put(SynchFIFO *p, FIFO_ENTRY v);
FIFO_ENTRY fifo_get(SynchFIFO *p);
int fifo_full(SynchFIFO *p);
int fifo_avail(SynchFIFO *p);
