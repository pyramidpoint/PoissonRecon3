#include "cuda_runtime.h"
#include "device_launch_parameters.h"

#include <stdio.h>
#include <malloc.h>
#include <random>
#include <time.h>
#include <math.h>


__global__ void marching_cubes_filter(int *lookup_two, int *mcIsLeaf, float * mcCubeValue, float *mcCubePosition, int sizeSolution, float *triangles, float Isovalue) {
	//printf("hello world\n");
	//const int global_x = blockIdx.x * blockDim.x + threadIdx.x;
	//const int global_y = blockIdx.y * blockDim.y + threadIdx.y;
	//const int global_z = blockIdx.z * blockDim.z + threadIdx.z;

	//const int node = global_z * data_width * data_height + global_y * data_width + global_x;

	//if(global_x + 1 < data_width-1 && global_y + 1 < data_height-1  && global_z + 1 < data_depth-1){
	int node = blockIdx.x * blockDim.x + threadIdx.x;

	if (node < sizeSolution)
	{

		if (mcIsLeaf[node] == 1)
		{
			//printf("node is %d\n", node);
			for (unsigned int tm = 0; tm<(5 * 3 * 3); tm++) {
				triangles[node* (5 * 3 * 3) + tm] = 0.0;
			}

			// double check that these refer to the right vertices
			float x = mcCubePosition[4 * node + 0];
			float y = mcCubePosition[4 * node + 1];
			float z = mcCubePosition[4 * node + 2];
			float width = mcCubePosition[4 * node + 3];
			float cube[8][3]{
				{ x, y, z },//0 0 0
				{ x + width, y,z },//1 0 0
				{ x + width,y + width, z },//1 1 0
				{ x , y + width, z },//0 1 0
				{ x, y, z + width },//0 0 1
				{ x, y + width, z + width },//0 1 1
				{ x + width, y + width, z + width },//1 1 1
				{ x + width, y, z + width } };//1 0 1
											  //edgeidx=i | (j << 1)) | (orientation << 2)  o j i
											  //o j i    x y z		 cube
											  //0 0 0(x) 0 0 0->1 0 0  0->1
											  //0 0 1    0 1 0->1 1 0  3->2
											  //0 1 0    0 0 1->1 0 1  4->7
											  //0 1 1    0 1 1->1 1 1  5->6
											  //1 0 0(y) 0 0 0->0 1 0  0->3
											  //1 0 1    1 0 0->1 1 0  1->2
											  //1 1 0    0 0 1->0 1 1  4->5
											  //1 1 1    1 0 1->1 1 1  7->6
											  //2 0 0(z) 0 0 0->0 0 1  0->4
											  //2 0 1    1 0 0->1 0 1  1->7
											  //2 1 0    0 1 0->0 1 1  3->5
											  //2 1 1    1 1 0->1 1 1  2->6

			int cubeindex[8] = { 1,2,8,4,16,32,128,64 };
			//根据顶点值得到索引
			//printf("position is %f %f %f\n", x, y, z);
			int case_lookup_idx = 0;
			for (unsigned int ci = 0; ci<8; ci++) {
				//const int x = cube[ci][0];
				//const int y = cube[ci][1];
				//const int z = cube[ci][2];


				//const int vertex = z * data_width * data_height + y * data_width + x;
				/*
				if (v[Cube::CornerIndex(0,0,0)] < iso) idx |=   1;
				if (v[Cube::CornerIndex(1,0,0)] < iso) idx |=   2;
				if (v[Cube::CornerIndex(1,1,0)] < iso) idx |=   4;
				if (v[Cube::CornerIndex(0,1,0)] < iso) idx |=   8;
				if (v[Cube::CornerIndex(0,0,1)] < iso) idx |=  16;
				if (v[Cube::CornerIndex(1,0,1)] < iso) idx |=  32;
				if (v[Cube::CornerIndex(1,1,1)] < iso) idx |=  64;
				if (v[Cube::CornerIndex(0,1,1)] < iso) idx |= 128;
				*/
				if (mcCubeValue[node * 8 + ci] <= Isovalue) {
					case_lookup_idx |= cubeindex[ci];
				}

			}
			for (int i = 0; i < 8; i++)
			{
				//printf("%f ", mcCubeValue[node * 8 + i]);
			}
			//printf("%f %f %f %f %f %f %f %f %d %d\n", mcCubeValue[node * 8 + 0], mcCubeValue[node * 8 + 1], mcCubeValue[node * 8 + 2], mcCubeValue[node * 8 + 3], mcCubeValue[node * 8 + 4], mcCubeValue[node * 8 + 5], mcCubeValue[node * 8 + 6], mcCubeValue[node * 8 + 7], case_lookup_idx,node);
			//根据索引得到边集合
			float edge_actual[12][6] = {

				{ cube[0][0],cube[0][1],cube[0][2],cube[1][0],cube[1][1],cube[1][2] },
				{ cube[3][0],cube[3][1],cube[3][2],cube[2][0],cube[2][1],cube[2][2] },
				{ cube[4][0],cube[4][1],cube[4][2],cube[7][0],cube[7][1],cube[7][2] },
				{ cube[5][0],cube[5][1],cube[5][2],cube[6][0],cube[6][1],cube[6][2] },

				{ cube[0][0],cube[0][1],cube[0][2],cube[3][0],cube[3][1],cube[3][2] },
				{ cube[1][0],cube[1][1],cube[1][2],cube[2][0],cube[2][1],cube[2][2] },
				{ cube[4][0],cube[4][1],cube[4][2],cube[5][0],cube[5][1],cube[5][2] },
				{ cube[7][0],cube[7][1],cube[7][2],cube[6][0],cube[6][1],cube[6][2] },

				{ cube[0][0],cube[0][1],cube[0][2],cube[4][0],cube[4][1],cube[4][2] },
				{ cube[1][0],cube[1][1],cube[1][2],cube[7][0],cube[7][1],cube[7][2] },
				{ cube[3][0],cube[3][1],cube[3][2],cube[5][0],cube[5][1],cube[5][2] },
				{ cube[2][0],cube[2][1],cube[2][2],cube[6][0],cube[6][1],cube[6][2] }

			};
			//edgeidx=i | (j << 1)) | (orientation << 2)  o j i
			//o j i    x y z		 cube
			//0 0 0(x) 0 0 0->1 0 0  0->1
			//0 0 1    0 1 0->1 1 0  3->2
			//0 1 0    0 0 1->1 0 1  4->7
			//0 1 1    0 1 1->1 1 1  5->6
			//1 0 0(y) 0 0 0->0 1 0  0->3
			//1 0 1    1 0 0->1 1 0  1->2
			//1 1 0    0 0 1->0 1 1  4->5
			//1 1 1    1 0 1->1 1 1  7->6
			//2 0 0(z) 0 0 0->0 0 1  0->4
			//2 0 1    1 0 0->1 0 1  1->7
			//2 1 0    0 1 0->0 1 1  3->5
			//2 1 1    1 1 0->1 1 1  2->6
			int edgecorner[12][6] = {
				{ 0,0,0,1,0,0 },
				{ 0,1,0,1,1,0 },
				{ 0,0,1,1,0,1 },
				{ 0,1,1,1,1,1 },
				{ 0,0,0,0,1,0 },
				{ 1,0,0,1,1,0 },
				{ 0,0,1,0,1,1 },
				{ 1,0,1,1,1,1 },
				{ 0,0,0,0,0,1 },
				{ 1,0,0,1,0,1 },
				{ 0,1,0,0,1,1 },
				{ 1,1,0,1,1,1 }
			};

			//printf("%d corresponding %d\n", node, case_lookup_idx);
			if (case_lookup_idx != 255 && case_lookup_idx != 0) {
				int current = 0;
				int edge_counter = 0;
				//printf("case_lookup_idx is %d\n", case_lookup_idx);
				for (int w = 0; w<16; w++) {
					current = lookup_two[case_lookup_idx * 16 + w];
					// current now gives an edge index so we need to add the point to the triangle list

					if (current != -1) {
						//printf("current!=1\n");
						float point1_x = edge_actual[current][0];
						float point1_y = edge_actual[current][1];
						float point1_z = edge_actual[current][2];
						int x_1 = edgecorner[current][0];
						int y_1 = edgecorner[current][1];
						int z_1 = edgecorner[current][2];
						int x_2 = edgecorner[current][3];
						int y_2 = edgecorner[current][4];
						int z_2 = edgecorner[current][5];
						float point2_x = edge_actual[current][3];
						float point2_y = edge_actual[current][4];
						float point2_z = edge_actual[current][5];
						int orientation = current >> 2;
						//iso-x0/x1-iso=1-x/x
						//x1-iso-x1x+siox=isox+xx0
						double averageRoot;
						//printf("x1 = %d %d %d\n", x_1, y_1, z_1);
						//printf("x2 = %d %d %d\n", x_2, y_2, z_2);
						//printf("x1=%d\n", (z_1 << 2) + (y_1 << 1) + x_1);
						//printf("x2=%d\n", (z_2 << 2) + (y_2 << 1) + x_2);
						//printf("point1_x is %f %f %f\n", point1_x, point1_y, point1_z);
						//printf("point2_x is %f %f %f\n", point2_x, point2_y, point2_z);
						//printf("%f %f %f\n", (point2_x - point1_x), (point2_y - point1_y), (point2_z - point1_z));
						averageRoot = (Isovalue - mcCubeValue[node * 8 + (z_1 << 2) + (y_1 << 1) + x_1]) / (mcCubeValue[node * 8 + (z_2 << 2) + (y_2 << 1) + x_2] - mcCubeValue[node * 8 + (z_1 << 2) + (y_1 << 1) + x_1]);
						//	printf("%f\n", averageRoot);
						//printf("Isovalue - mcCubeValue[%d*8+(z_1 << 2) + (y_1 << 1) + x_1] is %f\n",node, Isovalue - mcCubeValue[node * 8 + (z_1 << 2) + (y_1 << 1) + x_1]);
						//printf(" (mcCubeValue[%d*8+(z_2 << 2) + (y_2 << 1) + x_2]: %f - mcCubeValue[*8+(z_1 << 2) + (y_1 << 1) + x_1] )%f  is %f\n",node, mcCubeValue[node *8 + (z_2 << 2) + (y_2 << 1) + x_2], mcCubeValue[node * 8 + (z_1 << 2) + (y_1 << 1) + x_1],(mcCubeValue[node * 8 + (z_2 << 2) + (y_2 << 1) + x_2] - mcCubeValue[node * 8 + (z_1 << 2) + (y_1 << 1) + x_1]));
						//	printf("averageRoot is %f\n", averageRoot);
						if (orientation == 0)
						{
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 0] = (((float)point1_x + (float)width*averageRoot));
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 1] = (((float)point1_y + (float)point2_y) / 2.0);
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 2] = (((float)point1_z + (float)point2_z) / 2.0);// could do better interpolation here
						}
						else if (orientation == 1)
						{
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 0] = (((float)point1_x + (float)point2_x) / 2.0);
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 1] = (((float)point1_y + (float)width*averageRoot));
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 2] = (((float)point1_z + (float)point2_z) / 2.0);// could do better interpolation here
						}
						else if (orientation == 2)
						{
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 0] = (((float)point1_x + (float)point2_x) / 2.0);
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 1] = (((float)point1_y + (float)point2_y) / 2.0);
							triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 2] = (((float)point1_z + (float)width*averageRoot));// could do better interpolation here
						}
						//printf("%f %f %f  ", triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 0], triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 1], triangles[node * (5 * 3 * 3) + (edge_counter * 3) + 2]);




						edge_counter++;
					}



				}
				//printf("\n");
			}
		}
	}

}






extern "C"
cudaError_t MC(float * mcCubeValue, int * mcIsLeaf, float *mcCubePosition, int sizeSolution, float *trangles, float Isovalue)
{
	cudaFree(0);
	float *dev_mcCubeValue = NULL;
	float *dev_mcCubePosition = NULL;
	float *dev_trangles = NULL;
	int * dev_mcIsLeaf = NULL;
	int *device_lookup = NULL;

	cudaError_t cudaStatus;
	//printf("welcome to mulWithCudamatrixn\n");
	cudaStatus = cudaSetDevice(0);
	//printf("welcome to mulWithCudamatrixn\n");
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaSetDevice failed! Do you have a CUDA-capable GPU installed?\n");
		goto Error;
	}
	//else
	//{
	//	printf("CUDA-capable GPU has installed\n");
	//}


	cudaStatus = cudaMalloc((void **)&dev_mcCubeValue, sizeSolution * 8 * sizeof(float));
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMalloc dev_mcCubeValue failed!\n");
		goto Error;
	}
	//else
	//{
	//	printf("dev_dev_mcCubeValue has cudaMalloced\n");
	//}

	cudaStatus = cudaMalloc((void **)&dev_mcCubePosition, sizeSolution * 4 * sizeof(float));
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMalloc dev_mcCubePosition failed!\n");
		goto Error;
	}
	//else
	//{
	//	printf("dev_mcCubePosition has cudaMalloced\n");
	//}

	cudaStatus = cudaMalloc((void **)&dev_trangles, sizeSolution * 5 * 3 * 3 * sizeof(float));
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMalloc dev_trangles failed!\n");
		goto Error;
	}
	//else
	//{
	//	printf("dev_trangles has cudaMalloced\n");
	//}

	cudaStatus = cudaMalloc((void **)&dev_mcIsLeaf, sizeSolution * sizeof(int));
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMalloc dev_mcIsLeaf failed!\n");
		goto Error;
	}
	//else
	//{
	//	printf("dev_mcIsLeaf has cudaMalloced\n");
	//}



	cudaStatus = cudaMemcpy(dev_mcCubeValue, mcCubeValue, sizeSolution * 8 * sizeof(float), cudaMemcpyHostToDevice);
	if (cudaStatus != cudaSuccess)
	{
		printf("cudamemcpy dev_mcCubeValue failed!\n");
		goto Error;
	}

	cudaStatus = cudaMemcpy(dev_mcCubePosition, mcCubePosition, sizeSolution * 4 * sizeof(float), cudaMemcpyHostToDevice);
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMemcpy dev_mcCubePosition failed!\n");
		goto Error;
	}

	cudaStatus = cudaMemcpy(dev_mcIsLeaf, mcIsLeaf, sizeSolution * sizeof(int), cudaMemcpyHostToDevice);
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMemcpy dev_mcIsLeaf failed!\n");
		goto Error;
	}
	int triTable[256][16] =
	{
		{ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,   5,   8,   5,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   5,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,   1,   5,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,  11,   1,   9,   1,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,  11,   8,  11,   1,   8,   1,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   1,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   8,   0,  10,   0,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,   4,   1,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   8,   9,  10,   9,   5,  10,   5,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,  10,   4,  11,   4,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,  10,   8,  11,   8,   0,  11,   0,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,  11,  10,   9,  10,   4,   9,   4,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,  11,   8,  11,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   6,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 6,   2,   0,   4,   6,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 6,   2,   8,   5,   0,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   4,   6,   9,   5,   6,   2,   9,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   5,  11,   8,   6,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   5,  11,   6,   2,   0,   4,   6,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 6,   2,   8,   9,  11,   1,   9,   1,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,  11,   2,   2,  11,   1,   2,   1,   6,   6,   1,   4,  -1,  -1,  -1,  -1 },
		{ 1,  10,   4,   2,   8,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   0,   1,   6,   2,   1,  10,   6,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,   4,   1,  10,   8,   6,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   2,   9,   5,   6,   2,   5,   1,   6,   1,  10,   6,  -1,  -1,  -1,  -1 },
		{ 2,   8,   6,   4,   5,  11,   4,  11,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   2,   0,   6,   2,   5,  11,   6,   5,  10,   6,  11,  -1,  -1,  -1,  -1 },
		{ 9,  11,  10,   9,  10,   4,   9,   4,   0,   8,   6,   2,  -1,  -1,  -1,  -1 },
		{ 9,  11,   2,   2,  11,   6,  10,   6,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   2,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   9,   2,   4,   8,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   2,   7,   0,   7,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   5,   4,   2,   7,   4,   8,   2,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   9,   2,   5,  11,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   5,  11,   0,   4,   8,   9,   2,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   0,   2,   1,   2,   7,   1,   7,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   7,  11,   1,   2,   7,   1,   4,   2,   4,   8,   2,  -1,  -1,  -1,  -1 },
		{ 4,   1,  10,   9,   2,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   9,   2,   0,   1,  10,   0,  10,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   1,  10,   2,   7,   5,   0,   2,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,  10,   8,   1,  10,   2,   7,   1,   2,   5,   1,   7,  -1,  -1,  -1,  -1 },
		{ 7,   9,   2,  10,   4,   5,  11,  10,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,  10,   8,  11,   8,   0,  11,   0,   5,   9,   2,   7,  -1,  -1,  -1,  -1 },
		{ 11,  10,   7,   7,  10,   4,   7,   4,   2,   2,   4,   0,  -1,  -1,  -1,  -1 },
		{ 11,  10,   7,   7,  10,   2,   8,   2,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   9,   8,   6,   7,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   6,   7,   0,   4,   7,   9,   0,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 6,   7,   5,   8,   6,   5,   0,   8,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   6,   7,   5,   4,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,  11,   1,   8,   6,   7,   9,   8,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   6,   7,   0,   4,   7,   9,   0,   7,  11,   1,   5,  -1,  -1,  -1,  -1 },
		{ 8,   1,   0,  11,   1,   8,   6,  11,   8,   7,  11,   6,  -1,  -1,  -1,  -1 },
		{ 11,   6,   7,   1,   6,  11,   6,   1,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,  10,   4,   6,   7,   9,   6,   9,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   1,   9,   9,   1,  10,   9,  10,   7,   7,  10,   6,  -1,  -1,  -1,  -1 },
		{ 6,   7,   5,   8,   6,   5,   0,   8,   5,   1,  10,   4,  -1,  -1,  -1,  -1 },
		{ 1,   7,   5,  10,   7,   1,   7,  10,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,  10,   4,  11,   4,   5,   7,   9,   8,   6,   7,   8,  -1,  -1,  -1,  -1 },
		{ 0,   6,   9,   9,   6,   7,   6,   0,   5,   5,  11,  10,   5,  10,   6,  -1 },
		{ 8,   7,   0,   6,   7,   8,   4,   0,   7,  11,  10,   4,   7,  11,   4,  -1 },
		{ 11,  10,   6,  11,   6,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   7,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,  11,   7,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   5,   0,  11,   7,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   7,   3,   4,   8,   9,   5,   4,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   1,   5,   3,   5,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,   7,   3,   1,   5,   7,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   1,   0,   3,   0,   9,   3,   9,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   8,   9,   4,   8,   7,   3,   4,   7,   1,   4,   3,  -1,  -1,  -1,  -1 },
		{ 1,  10,   4,   3,  11,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,  11,   7,   8,   0,   1,  10,   8,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   1,  10,   5,   0,   9,  11,   7,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   8,   9,  10,   9,   5,  10,   5,   1,  11,   7,   3,  -1,  -1,  -1,  -1 },
		{ 4,   5,   7,   4,   7,   3,   4,   3,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   8,   3,   3,   8,   0,   3,   0,   7,   7,   0,   5,  -1,  -1,  -1,  -1 },
		{ 4,   3,  10,   4,   7,   3,   4,   0,   7,   0,   9,   7,  -1,  -1,  -1,  -1 },
		{ 10,   8,   3,   3,   8,   7,   9,   7,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   7,   3,   8,   6,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   7,   3,   2,   0,   4,   2,   4,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   7,   3,   8,   6,   2,   5,   0,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   4,   6,   9,   5,   6,   2,   9,   6,   3,  11,   7,  -1,  -1,  -1,  -1 },
		{ 8,   6,   2,   3,   1,   5,   3,   5,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   1,   5,   3,   5,   7,   6,   2,   0,   4,   6,   0,  -1,  -1,  -1,  -1 },
		{ 3,   1,   0,   3,   0,   9,   3,   9,   7,   2,   8,   6,  -1,  -1,  -1,  -1 },
		{ 9,   4,   2,   2,   4,   6,   4,   9,   7,   7,   3,   1,   7,   1,   4,  -1 },
		{ 8,   6,   2,  11,   7,   3,   4,   1,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   0,   1,   6,   2,   1,  10,   6,   1,  11,   7,   3,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,   4,   1,  10,   8,   6,   2,  11,   7,   3,  -1,  -1,  -1,  -1 },
		{ 11,   7,   3,   5,   2,   9,   5,   6,   2,   5,   1,   6,   1,  10,   6,  -1 },
		{ 4,   5,   7,   4,   7,   3,   4,   3,  10,   6,   2,   8,  -1,  -1,  -1,  -1 },
		{ 10,   5,   3,   3,   5,   7,   5,  10,   6,   6,   2,   0,   6,   0,   5,  -1 },
		{ 8,   6,   2,   4,   3,  10,   4,   7,   3,   4,   0,   7,   0,   9,   7,  -1 },
		{ 9,   7,  10,  10,   7,   3,  10,   6,   9,   6,   2,   9,  -1,  -1,  -1,  -1 },
		{ 3,  11,   9,   2,   3,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   8,   0,   2,   3,  11,   2,  11,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   2,   3,   0,   3,  11,   0,  11,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   3,   8,   8,   3,  11,   8,  11,   4,   4,  11,   5,  -1,  -1,  -1,  -1 },
		{ 2,   3,   1,   2,   1,   5,   2,   5,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   3,   1,   2,   1,   5,   2,   5,   9,   0,   4,   8,  -1,  -1,  -1,  -1 },
		{ 0,   2,   3,   0,   3,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   3,   8,   8,   3,   4,   1,   4,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,  10,   4,   9,   2,   3,  11,   9,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   8,   0,  10,   0,   1,   3,  11,   9,   2,   3,   9,  -1,  -1,  -1,  -1 },
		{ 0,   2,   3,   0,   3,  11,   0,  11,   5,   1,  10,   4,  -1,  -1,  -1,  -1 },
		{ 5,   2,  11,  11,   2,   3,   2,   5,   1,   1,  10,   8,   1,   8,   2,  -1 },
		{ 10,   2,   3,   9,   2,  10,   4,   9,  10,   5,   9,   4,  -1,  -1,  -1,  -1 },
		{ 5,  10,   0,   0,  10,   8,  10,   5,   9,   9,   2,   3,   9,   3,  10,  -1 },
		{ 0,   2,   4,   4,   2,  10,   3,  10,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   8,   2,  10,   2,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   9,   8,   3,  11,   8,   6,   3,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,  11,   9,   3,  11,   0,   4,   3,   0,   6,   3,   4,  -1,  -1,  -1,  -1 },
		{ 11,   5,   3,   5,   0,   3,   0,   6,   3,   0,   8,   6,  -1,  -1,  -1,  -1 },
		{ 3,   4,   6,  11,   4,   3,   4,  11,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   1,   6,   6,   1,   5,   6,   5,   8,   8,   5,   9,  -1,  -1,  -1,  -1 },
		{ 0,   6,   9,   4,   6,   0,   5,   9,   6,   3,   1,   5,   6,   3,   5,  -1 },
		{ 3,   1,   6,   6,   1,   8,   0,   8,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   1,   4,   3,   4,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   9,   8,   3,  11,   8,   6,   3,   8,   4,   1,  10,  -1,  -1,  -1,  -1 },
		{ 3,   9,   6,  11,   9,   3,  10,   6,   9,   0,   1,  10,   9,   0,  10,  -1 },
		{ 4,   1,  10,  11,   5,   3,   5,   0,   3,   0,   6,   3,   0,   8,   6,  -1 },
		{ 5,  10,   6,   1,  10,   5,   6,  11,   5,   6,   3,  11,  -1,  -1,  -1,  -1 },
		{ 10,   5,   3,   4,   5,  10,   6,   3,   5,   9,   8,   6,   5,   9,   6,  -1 },
		{ 6,   3,  10,   9,   0,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,  10,   0,   0,  10,   4,   0,   8,   3,   8,   6,   3,  -1,  -1,  -1,  -1 },
		{ 6,   3,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   6,  10,   0,   4,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,  10,   3,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   6,  10,   8,   9,   5,   8,   5,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   1,   5,  10,   3,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,   1,   5,  11,  10,   3,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,   0,   9,  11,   1,   0,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,  11,   8,  11,   1,   8,   1,   4,  10,   3,   6,  -1,  -1,  -1,  -1 },
		{ 4,   1,   3,   6,   4,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   1,   3,   8,   0,   3,   6,   8,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,   3,   6,   4,   1,   3,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,   6,   6,   9,   5,   6,   5,   3,   3,   5,   1,  -1,  -1,  -1,  -1 },
		{ 6,   4,   5,   6,   5,  11,   6,  11,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   6,   8,   0,   3,   6,   0,   5,   3,   5,  11,   3,  -1,  -1,  -1,  -1 },
		{ 3,   9,  11,   0,   9,   3,   6,   0,   3,   4,   0,   6,  -1,  -1,  -1,  -1 },
		{ 8,   9,   6,   6,   9,   3,  11,   3,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   8,  10,   3,   2,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   2,   0,  10,   3,   0,   4,  10,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,   8,  10,   3,   8,   3,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   3,   2,  10,   3,   9,   5,  10,   9,   4,  10,   5,  -1,  -1,  -1,  -1 },
		{ 11,   1,   5,   2,   8,  10,   3,   2,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   2,   0,  10,   3,   0,   4,  10,   0,   5,  11,   1,  -1,  -1,  -1,  -1 },
		{ 9,  11,   1,   9,   1,   0,   2,   8,  10,   3,   2,  10,  -1,  -1,  -1,  -1 },
		{ 10,   2,   4,   3,   2,  10,   1,   4,   2,   9,  11,   1,   2,   9,   1,  -1 },
		{ 1,   3,   2,   4,   1,   2,   8,   4,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   1,   3,   2,   0,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   3,   2,   4,   1,   2,   8,   4,   2,   9,   5,   0,  -1,  -1,  -1,  -1 },
		{ 9,   3,   2,   5,   3,   9,   3,   5,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   2,  11,  11,   2,   8,  11,   8,   5,   5,   8,   4,  -1,  -1,  -1,  -1 },
		{ 5,   2,   0,  11,   2,   5,   2,  11,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   3,   8,   8,   3,   2,   3,   4,   0,   0,   9,  11,   0,  11,   3,  -1 },
		{ 9,  11,   3,   9,   3,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,   9,   2,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   2,   7,  10,   3,   6,   0,   4,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,   7,   5,   0,   7,   0,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   5,   4,   2,   7,   4,   8,   2,   4,  10,   3,   6,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,   9,   2,   7,   1,   5,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,   9,   2,   7,   1,   5,  11,   0,   4,   8,  -1,  -1,  -1,  -1 },
		{ 1,   0,   2,   1,   2,   7,   1,   7,  11,   3,   6,  10,  -1,  -1,  -1,  -1 },
		{ 10,   3,   6,   1,   7,  11,   1,   2,   7,   1,   4,   2,   4,   8,   2,  -1 },
		{ 9,   2,   7,   6,   4,   1,   6,   1,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   1,   3,   8,   0,   3,   6,   8,   3,   7,   9,   2,  -1,  -1,  -1,  -1 },
		{ 0,   2,   7,   0,   7,   5,   4,   1,   3,   6,   4,   3,  -1,  -1,  -1,  -1 },
		{ 2,   5,   8,   7,   5,   2,   6,   8,   5,   1,   3,   6,   5,   1,   6,  -1 },
		{ 6,   4,   5,   6,   5,  11,   6,  11,   3,   7,   9,   2,  -1,  -1,  -1,  -1 },
		{ 9,   2,   7,   0,   6,   8,   0,   3,   6,   0,   5,   3,   5,  11,   3,  -1 },
		{ 3,   4,  11,   6,   4,   3,   7,  11,   4,   0,   2,   7,   4,   0,   7,  -1 },
		{ 11,   3,   8,   8,   3,   6,   8,   2,  11,   2,   7,  11,  -1,  -1,  -1,  -1 },
		{ 9,   8,  10,   7,   9,  10,   3,   7,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   0,   7,   0,   4,   7,   4,   3,   7,   4,  10,   3,  -1,  -1,  -1,  -1 },
		{ 8,  10,   0,   0,  10,   3,   0,   3,   5,   5,   3,   7,  -1,  -1,  -1,  -1 },
		{ 10,   5,   4,   3,   5,  10,   5,   3,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   8,  10,   7,   9,  10,   3,   7,  10,   1,   5,  11,  -1,  -1,  -1,  -1 },
		{ 1,   5,  11,   9,   0,   7,   0,   4,   7,   4,   3,   7,   4,  10,   3,  -1 },
		{ 11,   0,   7,   1,   0,  11,   3,   7,   0,   8,  10,   3,   0,   8,   3,  -1 },
		{ 7,   1,   4,  11,   1,   7,   4,   3,   7,   4,  10,   3,  -1,  -1,  -1,  -1 },
		{ 4,   9,   8,   7,   9,   4,   1,   7,   4,   3,   7,   1,  -1,  -1,  -1,  -1 },
		{ 7,   1,   3,   9,   1,   7,   1,   9,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   7,   0,   0,   7,   5,   7,   8,   4,   4,   1,   3,   4,   3,   7,  -1 },
		{ 5,   1,   3,   7,   5,   3,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   4,  11,  11,   4,   5,   4,   3,   7,   7,   9,   8,   7,   8,   4,  -1 },
		{ 3,   9,   0,   7,   9,   3,   0,  11,   3,   0,   5,  11,  -1,  -1,  -1,  -1 },
		{ 3,   7,  11,   8,   4,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 3,   7,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 6,  10,  11,   7,   6,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,  10,  11,   7,  10,   7,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   5,   0,   6,  10,  11,   7,   6,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,   5,   8,   5,   4,   6,  10,  11,   7,   6,  11,  -1,  -1,  -1,  -1 },
		{ 5,   7,   6,   5,   6,  10,   5,  10,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   7,   6,   5,   6,  10,   5,  10,   1,   4,   8,   0,  -1,  -1,  -1,  -1 },
		{ 1,   0,  10,  10,   0,   9,  10,   9,   6,   6,   9,   7,  -1,  -1,  -1,  -1 },
		{ 1,   7,  10,  10,   7,   6,   7,   1,   4,   4,   8,   9,   4,   9,   7,  -1 },
		{ 7,   6,   4,   7,   4,   1,   7,   1,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   0,   1,   8,   0,  11,   7,   8,  11,   6,   8,   7,  -1,  -1,  -1,  -1 },
		{ 7,   6,   4,   7,   4,   1,   7,   1,  11,   5,   0,   9,  -1,  -1,  -1,  -1 },
		{ 11,   6,   1,   7,   6,  11,   5,   1,   6,   8,   9,   5,   6,   8,   5,  -1 },
		{ 4,   5,   7,   4,   7,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   7,   0,   0,   7,   8,   6,   8,   7,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   6,   9,   9,   6,   0,   4,   0,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   9,   7,   8,   7,   6,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,  10,  11,   2,   8,  11,   7,   2,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,  11,   4,   4,  11,   7,   4,   7,   0,   0,   7,   2,  -1,  -1,  -1,  -1 },
		{ 8,  10,  11,   2,   8,  11,   7,   2,  11,   5,   0,   9,  -1,  -1,  -1,  -1 },
		{ 9,   4,   2,   5,   4,   9,   7,   2,   4,  10,  11,   7,   4,  10,   7,  -1 },
		{ 1,   8,  10,   2,   8,   1,   5,   2,   1,   7,   2,   5,  -1,  -1,  -1,  -1 },
		{ 1,   7,  10,   5,   7,   1,   4,  10,   7,   2,   0,   4,   7,   2,   4,  -1 },
		{ 7,   1,   9,   9,   1,   0,   1,   7,   2,   2,   8,  10,   2,  10,   1,  -1 },
		{ 7,   2,   9,  10,   1,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   4,   2,   4,   1,   2,   1,   7,   2,   1,  11,   7,  -1,  -1,  -1,  -1 },
		{ 11,   0,   1,   7,   0,  11,   0,   7,   2,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   0,   9,   8,   4,   2,   4,   1,   2,   1,   7,   2,   1,  11,   7,  -1 },
		{ 2,   5,   1,   9,   5,   2,   1,   7,   2,   1,  11,   7,  -1,  -1,  -1,  -1 },
		{ 4,   5,   8,   8,   5,   2,   7,   2,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   2,   0,   5,   7,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 7,   2,   4,   4,   2,   8,   4,   0,   7,   0,   9,   7,  -1,  -1,  -1,  -1 },
		{ 7,   2,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,  11,   9,   6,  10,   9,   2,   6,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,  11,   9,   6,  10,   9,   2,   6,   9,   0,   4,   8,  -1,  -1,  -1,  -1 },
		{ 5,  10,  11,   6,  10,   5,   0,   6,   5,   2,   6,   0,  -1,  -1,  -1,  -1 },
		{ 2,   5,   8,   8,   5,   4,   5,   2,   6,   6,  10,  11,   6,  11,   5,  -1 },
		{ 10,   1,   6,   1,   5,   6,   5,   2,   6,   5,   9,   2,  -1,  -1,  -1,  -1 },
		{ 0,   4,   8,  10,   1,   6,   1,   5,   6,   5,   2,   6,   5,   9,   2,  -1 },
		{ 1,   0,  10,  10,   0,   6,   2,   6,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   6,   1,   1,   6,  10,   1,   4,   2,   4,   8,   2,  -1,  -1,  -1,  -1 },
		{ 11,   9,   1,   1,   9,   2,   1,   2,   4,   4,   2,   6,  -1,  -1,  -1,  -1 },
		{ 8,   1,   6,   0,   1,   8,   2,   6,   1,  11,   9,   2,   1,  11,   2,  -1 },
		{ 11,   6,   1,   1,   6,   4,   6,  11,   5,   5,   0,   2,   5,   2,   6,  -1 },
		{ 2,   6,   8,  11,   5,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 6,   4,   2,   2,   4,   9,   5,   9,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   9,   6,   6,   9,   2,   6,   8,   5,   8,   0,   5,  -1,  -1,  -1,  -1 },
		{ 0,   2,   6,   0,   6,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 2,   6,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,  10,  11,   9,   8,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 0,  11,   9,   4,  11,   0,  11,   4,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,  10,  11,   0,  10,   5,  10,   0,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,  10,  11,   5,   4,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,   8,  10,   5,   8,   1,   8,   5,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   4,  10,   0,   4,   9,  10,   5,   9,  10,   1,   5,  -1,  -1,  -1,  -1 },
		{ 0,   8,  10,   1,   0,  10,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 10,   1,   4,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 4,   9,   8,   1,   9,   4,   9,   1,  11,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 1,  11,   9,   0,   1,   9,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 11,   0,   8,   5,   0,  11,   8,   1,  11,   8,   4,   1,  -1,  -1,  -1,  -1 },
		{ 11,   5,   1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 5,   9,   8,   4,   5,   8,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 9,   0,   5,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ 8,   4,   0,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 },
		{ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1 }
	};
	int *host_lookup = NULL;
	host_lookup = (int *)malloc(sizeof(int) * 256 * 16);

	for (int i = 0; i<256; i++) {
		for (int j = 0; j<16; j++) {
			host_lookup[i * 16 + j] = triTable[i][j];
		}
	}


	//std::cout << "\nAllocating Cubes memory\n";

	cudaStatus = cudaMalloc((void **)&device_lookup, sizeof(int) * 256 * 16);
	if (cudaStatus != cudaSuccess)
	{
		printf("cudamMalloc device_lookup failed!\n");
		goto Error;
	}
	cudaStatus = cudaMemcpy(device_lookup, host_lookup, sizeof(int) * 256 * 16, cudaMemcpyHostToDevice);
	if (cudaStatus != cudaSuccess)
	{
		printf("cudamMemcpy device_lookup failed!\n");
		goto Error;
	}
	cudaEvent_t gpuStart, gpuFinish;
	float elapsedTime;
	cudaEventCreate(&gpuStart);
	cudaEventCreate(&gpuFinish);
	cudaEventRecord(gpuStart, 0);

	/*const int THREADNUM = 256;
	const int BLOCKNUM = (M * S + 255) / 256;*/

	//const int BLOCK_SIZE = 128;
	//dim3 block(BLOCK_SIZE);
	//dim3 grid(BLOCK_SIZE);
	int threadSize = 256;

	long blockSize = (sizeSolution + threadSize - 1) / threadSize;
	/*printf("blocksize:%d threadsize:%d\n", blockSize, threadSize);
	printf("gpuMatMultKernel starts\n");*/
	marching_cubes_filter << <blockSize, threadSize >> > (device_lookup, dev_mcIsLeaf, dev_mcCubeValue, dev_mcCubePosition, sizeSolution, dev_trangles, Isovalue);
	//gpuMatMultWithSharedKernelmatirx<32> << <grid, block >> >(dev_a, dev_b, dev_result, M, N, S);
	//gpuMatMultKernel(float* source_point, float* zuobiao, int * ngbr, float* results_point, int sizeup)
	cudaEventRecord(gpuFinish, 0);
	cudaEventSynchronize(gpuFinish);
	cudaEventElapsedTime(&elapsedTime, gpuStart, gpuFinish);
	//printf("\nThe runing time of GPU on Mat Multiply is %f seconds.\n", elapsedTime / 1000.0);

	cudaStatus = cudaGetLastError();
	if (cudaStatus != cudaSuccess)
	{
		printf("MulKernel launch failed: %s!\n", cudaGetErrorString(cudaStatus));
		goto Error;
	}

	cudaStatus = cudaDeviceSynchronize();
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaDeviceSynchronize return Error code %d after Kernel launched!\n", cudaStatus);
		goto Error;
	}

	cudaStatus = cudaMemcpy(trangles, dev_trangles, sizeSolution * 5 * 3 * 3 * sizeof(float), cudaMemcpyDeviceToHost);
	if (cudaStatus != cudaSuccess)
	{
		printf("cudaMemcpy result failed!\n");
		goto Error;
	}

Error:
	cudaFree(dev_mcCubeValue);
	cudaFree(dev_mcCubePosition);
	cudaFree(dev_trangles);
	cudaFree(dev_mcIsLeaf);
	cudaFree(host_lookup);


	return cudaStatus;
}