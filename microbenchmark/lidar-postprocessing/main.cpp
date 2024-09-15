#include <iostream>
#include <fstream>
#include <stdio.h>
#include <string.h>
#include <chrono>

#include <papi.h>

void handle_error (int retval)
{
     printf("PAPI error %d: %s\n", retval, PAPI_strerror(retval));
     exit(1);
}


float* offset_map_ = 0;
float* prob_map_ = 0;
int rows_ = 864;
int cols_ = 864;

// Make p[i] to be the reference of a's ith row, where a is a mxn matrix
template <typename T>
inline void IMakeReference(T *a, T **p, int m, int n) {
  for (int i = 0; i < m; i++) {
    p[i] = &a[i * n];
  }
}

// Memory allocated with this function must be released with IFree2
template <typename T>
inline T **IAlloc2(int m, int n) {
  T *mem = nullptr;
  T **head = nullptr;
  mem = new (std::nothrow) T[m * n];
  if (mem == nullptr) {
    return nullptr;
  }
  head = new (std::nothrow) T *[m];
  if (head == nullptr) {
    delete[] mem;
    return nullptr;
  }
  IMakeReference<T>(mem, head, m, n);
  return head;
}

// Free memory allocated with function IAlloc2
template <typename T>
inline void IFree2(T ***A) {
  if (A != nullptr && *A != nullptr) {
    delete[](*A)[0];
    delete[] * A;
    *A = nullptr;
  }
}

struct Node {
    uint32_t center_node = 0;
    uint32_t parent = 0;
    uint16_t id = 0;
    // Note, we compress node_rank, traversed, is_center and is_object
    // in one 16bits variable, the arrangemant is as following
    // |is_center(1bit)|is_object(1bit)|traversed(3bit)|node_rank(11bit)|
    uint16_t status = 0;

    inline uint16_t get_node_rank() { return status & 2047; }
    inline void set_node_rank(uint16_t node_rank) {
      status &= 63488;
      status |= node_rank;
    }
    inline uint16_t get_traversed() {
      uint16_t pattern = 14336;
      return static_cast<uint16_t>((status & pattern) >> 11);
    }
    inline void set_traversed(uint16_t traversed) {
      status &= 51199;
      uint16_t pattern = 7;
      status |= static_cast<uint16_t>((traversed & pattern) << 11);
    }
    inline bool is_center() { return static_cast<bool>(status & 32768); }
    inline void set_is_center(bool is_center) {
      if (is_center) {
        status |= 32768;
      } else {
        status &= 32767;
      }
    }
    inline bool is_object() { return static_cast<bool>(status & 16384); }
    inline void set_is_object(bool is_object) {
      if (is_object) {
        status |= 16384;  // 2^14
      } else {
        status &= 49151;  // 65535 - 2^14
      }
    }
  };

Node** nodes_ = 0;

void read_file(){
	std::ifstream infile("./spp.txt");


	offset_map_ = (float*)(malloc(sizeof(float)*1492992));
	prob_map_ = (float*)(malloc(sizeof(float)*746496));
	
	float cnt = 0;
	for(int i=0;i<1492992;i++){
		infile >> offset_map_[i];
		cnt += offset_map_[i];	
	}	
	std::cout << cnt << "\n";
	cnt = 0;
	for(int i=0;i<746496;i++){
		infile >> prob_map_[i];
		cnt += prob_map_[i];	
	}	
	std::cout << cnt << "\n";

	infile.close();

	nodes_ = IAlloc2<Node>(864,864);
	
	memset(nodes_[0],0,sizeof(Node)*rows_*cols_);
}

float objectness_threshold_ = 0.5f;
float scale_ = 4.8f;
bool BuildNodes(int start_row_index, int end_row_index) {
  
 
  const float* offset_row_ptr = offset_map_ + start_row_index * cols_;
  const float* offset_col_ptr = offset_map_ + (rows_ + start_row_index) * cols_;
//  const float* prob_map_ptr = prob_map_[0] + start_row_index * cols_;
  Node* node_ptr = nodes_[0] + start_row_index * cols_;
  for (int row = start_row_index; row < end_row_index; ++row) {
    for (int col = 0; col < cols_; ++col) {
      node_ptr->set_is_object(*prob_map_++ >= objectness_threshold_);
      int center_row = static_cast<int>(*offset_row_ptr++ * scale_ +
                                        static_cast<float>(row) + 0.5f);
      int center_col = static_cast<int>(*offset_col_ptr++ * scale_ +
                                        static_cast<float>(col) + 0.5f);
      center_row = std::max(0, std::min(rows_ - 1, center_row));
      center_col = std::max(0, std::min(cols_ - 1, center_col));
      (node_ptr++)->center_node = center_row * cols_ + center_col;
    }
  }
  return true;
}

float* evict_map_ = 0;
void evict(){
	evict_map_ = (float*)malloc(sizeof(float)*3000000);
	for(int i=0;i<3000000;i++)
		evict_map_[i] = i*1.0/3000;
}

main(){

//	exit(0);
	read_file();

	//evict cache
//	evict();
int EventSet;
	int i,sum;

	long_long values[2], values1[2], values2[2];

	if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT)
		exit(-1);

	EventSet = PAPI_NULL;

	if (PAPI_create_eventset(&EventSet) != PAPI_OK)
		exit(-1);

	if (PAPI_add_event(EventSet, PAPI_L2_TCM) != PAPI_OK)
		exit(-1);

	if (PAPI_add_event(EventSet, PAPI_L3_TCM) != PAPI_OK)
		 exit(-1);
	
	if (PAPI_start(EventSet) != PAPI_OK)
		exit(-1);

	if (PAPI_read(EventSet, values1) != PAPI_OK)
		exit(-1);	

	  auto t0 = std::chrono::high_resolution_clock::now();

	BuildNodes(0, 864);
  auto t1 = std::chrono::high_resolution_clock::now();
	  std::chrono::duration< double > fs = t1 - t0;
  std::chrono::microseconds d = std::chrono::duration_cast< std::chrono::microseconds >( fs );

//  std::cout << fs.count() << "s\n";
  long long ms = d.count();
  std::cout << (ms*1.0)/1000.0 << "ms\n";
  //   std::cout << "data collect time: "<< t2 << "\n";
	std::cout << "Hello World" << std::endl;

	if(PAPI_stop(EventSet, values2) != PAPI_OK)
		exit(-1);

	if(PAPI_cleanup_eventset(EventSet) != PAPI_OK)
 		exit(-1);

 	if(PAPI_destroy_eventset(&EventSet) != PAPI_OK)
 		exit(-1);

 	PAPI_shutdown();

	values[0]=values2[0]-values1[0];

 values[1]=values2[1]-values1[1];

 printf("L2_TCM:%lld\nL3_TCM: %lld\n",values[0], values[1]);

	IFree2(&nodes_);
//	free(offset_map_);
//	free(prob_map_);
//	return 0;
}
