/*
Copyright (c) 2006, Michael Kazhdan and Matthew Bolitho
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of
conditions and the following disclaimer. Redistributions in binary form must reproduce
the above copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the distribution. 

Neither the name of the Johns Hopkins University nor the names of its contributors
may be used to endorse or promote products derived from this software without specific
prior written permission. 

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO THE IMPLIED WARRANTIES 
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
*/

#include "Octree.h"
#include "time.h"
#include<time.h>
#include "MemoryUsage.h"
#include <stdio.h>
#include <fstream>
#include<Eigen/Sparse>
#include<Eigen/IterativeLinearSolvers>
#include<Eigen/SparseCholesky>
//#include<Eigen/RequiredModuleName>
#include <cmath>
#include "cuda_runtime.h"
#include "device_launch_parameters.h"
#include<stdlib.h>
#include <algorithm>
#include<iostream>
using namespace std;

extern "C"
cudaError_t MC(float * mcCubeValue, int * mcIsLeaf, float *mcCubePosition, int sizeSolution, float *trangles, float Isovalue);
#define PI 3.141592653
#define ITERATION_POWER 1.0/3
#define MEMORY_ALLOCATOR_BLOCK_SIZE 1<<12

#define READ_SIZE 1024

#define PAD_SIZE (Real(1.0))

const Real EPSILON=Real(1e-6);
const Real ROUND_EPS=Real(1e-5);


/////////////////////
// SortedTreeNodes //
/////////////////////
SortedTreeNodes::SortedTreeNodes(void){
	nodeCount=NULL;
	treeNodes=NULL;
	maxDepth=0;
}
SortedTreeNodes::~SortedTreeNodes(void){
	if(nodeCount){delete[] nodeCount;}
	if(treeNodes){delete[] treeNodes;}
	nodeCount=NULL;
	treeNodes=NULL;
}
void SortedTreeNodes::set(TreeOctNode& root,const int& setIndex){
	if(nodeCount){delete[] nodeCount;}
	if(treeNodes){delete[] treeNodes;}
	maxDepth=root.maxDepth()+1;
	nodeCount=new int[maxDepth+1];
	treeNodes=new TreeOctNode*[root.nodes()];

	TreeOctNode* temp=root.nextNode();
	int i,cnt=0;
	while(temp){
		treeNodes[cnt++]=temp;
		temp=root.nextNode(temp);
	}
	qsort(treeNodes,cnt,sizeof(const TreeOctNode*),TreeOctNode::CompareForwardPointerDepths);
	for(i=0;i<=maxDepth;i++){nodeCount[i]=0;}
	for(i=0;i<cnt;i++){
		if(setIndex){treeNodes[i]->nodeData.nodeIndex=i;}
		nodeCount[treeNodes[i]->depth()+1]++;
	}
	for(i=1;i<=maxDepth;i++){nodeCount[i]+=nodeCount[i-1];}
}


//////////////////
// TreeNodeData //
//////////////////
int TreeNodeData::UseIndex=1;
TreeNodeData::TreeNodeData(void){
	if(UseIndex){
		nodeIndex=-1;
		centerWeightContribution=0;
	}
	else{mcIndex=0;}
	value=0;
}
TreeNodeData::~TreeNodeData(void){;}


////////////
// Octree //
////////////
template<int Degree>
double Octree<Degree>::maxMemoryUsage=0;

template<int Degree>
double Octree<Degree>::MemoryUsage(void){
	double mem=MemoryInfo::Usage()/(1<<20);
	if(mem>maxMemoryUsage){maxMemoryUsage=mem;}
	return mem;
}

template<int Degree>
Octree<Degree>::Octree(void){
	radius=0;
	width=0;
	postNormalSmooth=0;
}

template<int Degree>
void Octree<Degree>::setNodeIndices(TreeOctNode& node,int& idx){
	node.nodeData.nodeIndex=idx;
	idx++;
	if(node.children){for(int i=0;i<Cube::CORNERS;i++){setNodeIndices(node.children[i],idx);}}
}
template<int Degree>
int Octree<Degree>::NonLinearSplatOrientedPoint(TreeOctNode* node,const Point3D<Real>& position,const Point3D<Real>& normal){
	double x,dxdy,dxdydz,dx[DIMENSION][3];
	int i,j,k;
	TreeOctNode::Neighbors& neighbors=neighborKey.setNeighbors(node);
	double width;
	Point3D<Real> center;
	Real w;

	node->centerAndWidth(center,w);
	width=w;
	for(int i=0;i<3;i++){
		x=(center.coords[i]-position.coords[i]-width)/width;
		dx[i][0]=1.125+1.500*x+0.500*x*x;
		x=(center.coords[i]-position.coords[i])/width;
		dx[i][1]=0.750        -      x*x;
		dx[i][2]=1.0-dx[i][1]-dx[i][0];
	}
	std::vector<ngbrCoe<Real>> ngbr;
	for(i=0;i<3;i++){
		for(j=0;j<3;j++){
			dxdy=dx[0][i]*dx[1][j];
			for(k=0;k<3;k++){
				if(neighbors.neighbors[i][j][k]){
					dxdydz=dxdy*dx[2][k];
					//nodeIndex是在8*normal大小的normal域中的索引
					int idx=neighbors.neighbors[i][j][k]->nodeData.nodeIndex;
					if(idx<0){
						Point3D<Real> n;
						n.coords[0]=n.coords[1]=n.coords[2]=0;
						idx=neighbors.neighbors[i][j][k]->nodeData.nodeIndex=int(normals->size());
						//printf("normals->size() %d\n", int(normals->size()));
						normals->push_back(n);
					}
					//cout << Real(normal.coords[0] * dxdydz) << " " << Real(normal.coords[1] * dxdydz) << " " << Real(normal.coords[2] * dxdydz)<<endl;;
					(*normals)[idx].coords[0]+=Real(normal.coords[0]*dxdydz);
					(*normals)[idx].coords[1]+=Real(normal.coords[1]*dxdydz);
					(*normals)[idx].coords[2]+=Real(normal.coords[2]*dxdydz);
					ngbrCoe<Real> Coe;
					Coe.index =idx ;
					Coe.value = Real( dxdydz);
					ngbr.push_back(Coe);
					

				}
			}
		}
	}
	ngbrs->push_back(ngbr);
	return 0;
}
template<int Degree>
void Octree<Degree>::NonLinearSplatOrientedPoint(const Point3D<Real>& position,const Point3D<Real>& normal,const int& splatDepth,const Real& samplesPerNode,
												 const int& minDepth,const int& maxDepth){
	double dx;
	Point3D<Real> n;
	TreeOctNode* temp;
	int i,cnt=0;
	double width;
	Point3D<Real> myCenter;
	Real myWidth;
	myCenter.coords[0]=myCenter.coords[1]=myCenter.coords[2]=Real(0.5);
	myWidth=Real(1.0);

	temp=&tree;
	while(temp->depth()<splatDepth){
		if(!temp->children){
			printf("Octree<Degree>::NonLinearSplatOrientedPoint error\n");
			return;
		}
		int cIndex=TreeOctNode::CornerIndex(myCenter,position);
		temp=&temp->children[cIndex];
		Point3D< Real > start;
		Real w;
		
		temp->centerAndWidth(start, w);
		//cout << "start " << start[0] << " " << start[1] << " "<<start[2] << endl;
		myWidth/=2;
		if(cIndex&1){myCenter.coords[0]+=myWidth/2;}
		else		{myCenter.coords[0]-=myWidth/2;}
		if(cIndex&2){myCenter.coords[1]+=myWidth/2;}
		else		{myCenter.coords[1]-=myWidth/2;}
		if(cIndex&4){myCenter.coords[2]+=myWidth/2;}
		else		{myCenter.coords[2]-=myWidth/2;}
	}
	Real alpha,newDepth;
	NonLinearGetSampleDepthAndWeight(temp,position,samplesPerNode,newDepth,alpha);
	Point3D<Real> center;
	Real width1;
	temp->centerAndWidth(center, width1);
	//printf("%f %d %f %f %f %f %f\n", position[0], temp->depth(), center[0], center[1], center[2], alpha, newDepth);
	//std::cout << position[0] << " " << temp->depth() << " " << center[0] << " " << center[1] << " " << center[2] << " " << alpha << " " << newDepth << endl;
	if(newDepth<minDepth){newDepth=Real(minDepth);}
	if(newDepth>maxDepth){newDepth=Real(maxDepth);}
	int topDepth=int(ceil(newDepth));

	dx=1.0-(topDepth-newDepth);
	if(topDepth<=minDepth){
		topDepth=minDepth;
		dx=1;
	}
	else if(topDepth>maxDepth){
		topDepth=maxDepth;
		dx=1;
	}
	while(temp->depth()>topDepth){temp=temp->parent;}
	while(temp->depth()<topDepth){
		if(!temp->children){temp->initChildren();}
		int cIndex=TreeOctNode::CornerIndex(myCenter,position);
		temp=&temp->children[cIndex];
		myWidth/=2;
		if(cIndex&1){myCenter.coords[0]+=myWidth/2;}
		else		{myCenter.coords[0]-=myWidth/2;}
		if(cIndex&2){myCenter.coords[1]+=myWidth/2;}
		else		{myCenter.coords[1]-=myWidth/2;}
		if(cIndex&4){myCenter.coords[2]+=myWidth/2;}
		else		{myCenter.coords[2]-=myWidth/2;}
	}
	width=1.0/(1<<temp->depth());
	for(i=0;i<DIMENSION;i++){n.coords[i]=normal.coords[i]*alpha/Real(pow(width,3))*Real(dx);}
	NonLinearSplatOrientedPoint(temp,position,n);
	if(fabs(1.0-dx)>EPSILON){
		dx=Real(1.0-dx);
		temp=temp->parent;
		width=1.0/(1<<temp->depth());

		for(i=0;i<DIMENSION;i++){n.coords[i]=normal.coords[i]*alpha/Real(pow(width,3))*Real(dx);}
		NonLinearSplatOrientedPoint(temp,position,n);
	}
}
template<int Degree>
void Octree<Degree>::NonLinearGetSampleDepthAndWeight(TreeOctNode* node,const Point3D<Real>& position,const Real& samplesPerNode,Real& depth,Real& weight){
	TreeOctNode* temp=node;
	weight=Real(1.0)/NonLinearGetSampleWeight(temp,position);
	//cout << weight << endl;
#if NEW_SAMPLES_PER_NODE
	//***0
	if (weight >= (Real)1.) depth = Real(temp->depth()  + log(weight) / log(double(1 << (DIMENSION - 1))));
	//***1
	//**p0
	//if( weight>=samplesPerNode ) depth=Real( temp->depth()+log( weight/samplesPerNode )/log(double(1<<(DIMENSION-1))));
	//**p1
#else // !NEW_SAMPLES_PER_NODE
	if(weight>=samplesPerNode+1){depth=Real(temp->depth()+log(weight/(samplesPerNode+1))/log(double(1<<(DIMENSION-1))));}
#endif // NEW_SAMPLES_PER_NODE
	else{
		Real oldAlpha,newAlpha;
		oldAlpha=newAlpha=weight;
#if NEW_SAMPLES_PER_NODE
		//**p0
		//while(newAlpha<samplesPerNode && temp->parent){
		//**p1
		//***0
		while (newAlpha<(Real)1. && temp->parent) {
		//***1
#else // !NEW_SAMPLES_PER_NODE
		while(newAlpha<(samplesPerNode+1) && temp->parent){
#endif // NEW_SAMPLES_PER_NODE
			temp=temp->parent;
			oldAlpha=newAlpha;
			newAlpha=Real(1.0)/NonLinearGetSampleWeight(temp,position);
		}
#if NEW_SAMPLES_PER_NODE
		//**p0
		//depth=Real(temp->depth()+log(newAlpha/samplesPerNode)/log(newAlpha/oldAlpha));
		//**p1
		//***0
		depth = Real(temp->depth() + log(newAlpha) / log(newAlpha / oldAlpha));
		//***1
#else // !NEW_SAMPLES_PER_NODE
		depth=Real(temp->depth()+log(newAlpha/(samplesPerNode+1))/log(newAlpha/oldAlpha));
#endif // NEW_SAMPLES_PER_NODE
	}
	weight=Real(pow(double(1<<(DIMENSION-1)),-double(depth)));
}

template<int Degree>
Real Octree<Degree>::NonLinearGetSampleWeight(TreeOctNode* node,const Point3D<Real>& position){
	Real weight=0;
	double x,dxdy,dx[DIMENSION][3];
	int i,j,k;
	TreeOctNode::Neighbors& neighbors=neighborKey.setNeighbors(node);
	double width;
	Point3D<Real> center;
	Real w;
	node->centerAndWidth(center,w);
	width=w;

	for(i=0;i<DIMENSION;i++){
		x=(center.coords[i]-position.coords[i]-width)/width;
		dx[i][0]=1.125+1.500*x+0.500*x*x;
		x=(center.coords[i]-position.coords[i])/width;
		dx[i][1]=0.750        -      x*x;
		dx[i][2]=1.0-dx[i][1]-dx[i][0];
	}

	for(i=0;i<3;i++){
		for(j=0;j<3;j++){
			dxdy=dx[0][i]*dx[1][j];
			for(k=0;k<3;k++){
				if(neighbors.neighbors[i][j][k]){
					weight+=Real(dxdy*dx[2][k]*neighbors.neighbors[i][j][k]->nodeData.centerWeightContribution);
				}
			}
		}
	}
	return Real(1.0/weight);
}

template<int Degree>
int Octree<Degree>::NonLinearUpdateWeightContribution(TreeOctNode* node,const Point3D<Real>& position,const Real& weight){
	int i,j,k;
	TreeOctNode::Neighbors& neighbors=neighborKey.setNeighbors(node);
	double x,dxdy,dx[DIMENSION][3];
	double width;
	Point3D<Real> center;
	Real w;
	node->centerAndWidth(center,w);
	width=w;
#if NEW_SAMPLES_PER_NODE
	const double SAMPLE_SCALE = 1. / ( 0.125 * 0.125 + 0.75 * 0.75 + 0.125 * 0.125 );
#endif // NEW_SAMPLES_PER_NODE

	for(i=0;i<DIMENSION;i++){
		x=(center.coords[i]-position.coords[i]-width)/width;
		dx[i][0]=1.125+1.500*x+0.500*x*x;
		x=(center.coords[i]-position.coords[i])/width;
		dx[i][1]=0.750        -      x*x;
		dx[i][2]=1.0-dx[i][1]-dx[i][0];
#if NEW_SAMPLES_PER_NODE
		// Note that we are splatting along a co-dimension one manifold, so uniform point samples
		// do not generate a unit sample weight.
		//**p0
		//dx[i][0] *= SAMPLE_SCALE;
		//**p1
#endif // NEW_SAMPLES_PER_NODE
	}
	//***0
	//printf("%f\n", SAMPLE_SCALE);
	//for (int m = 0; m < 3; m++)
	//{

	//	printf("%lf %lf %lf\n", dx[m][0], dx[m][1], dx[m][2]);

	//}
	//weight *= (Real)SAMPLE_SCALE;
	//***1
	for(i=0;i<3;i++){
		for(j=0;j<3;j++){
			//**p0
			//dxdy=dx[0][i]*dx[1][j]*weight;
			//**p0
			//***0
			dxdy = dx[0][i] * dx[1][j] * weight*SAMPLE_SCALE;
			//***1
			for(k=0;k<3;k++){
				if(neighbors.neighbors[i][j][k]){neighbors.neighbors[i][j][k]->nodeData.centerWeightContribution+=Real(dxdy*dx[2][k]);}
			}
		}
	}
	//cout << " start" <<w<< center[0]-w/2<<" "<< center[1] - w / 2 <<" "<< center[2] - w / 2 <<" "<< neighbors.neighbors[1][1][1]->nodeData.centerWeightContribution << endl;
	//cout <<" addWeightContribution "<< neighbors.neighbors[1][1][1]->nodeData.centerWeightContribution << endl;
	return 0;
}
template< int Degree > bool Octree< Degree >::_InBounds(Point3D< Real > p) { return p[0] >= Real(0.) && p[0] <= Real(1.0) && p[1] >= Real(0.) && p[1] <= Real(1.0) && p[2] >= Real(0.) && p[2] <= Real(1.0); }


//***0

template<int Degree>
void Octree<Degree>::setDensityEstimator(std::vector<PointSample>& samples, const int kernelDepth, Real samplesPerNode)
{
	int splatDepth = kernelDepth;
	for (int i = 0; i<samples.size(); i++)
	{
		const TreeOctNode* node = samples[i].node;
		const ProjectiveData< OrientedPoint3D< Real >, Real >& sample = samples[i].sample;
		if (sample.weight>0)
		{
			Point3D< Real > p = sample.data.p / sample.weight;
			Real w = sample.weight / samplesPerNode;
			for (TreeOctNode* _node = (TreeOctNode*)node; _node; _node = _node->parent)
			{
				if (_node->depth() <= splatDepth)
				{
					NonLinearUpdateWeightContribution(_node, p, w);
				}
			}

		}
	}
}


template<int Degree>
void Octree<Degree>::normalField(std::vector<PointSample>& samples, const int& kernelDepth, const Real& samplesPerNode, const int& maxDepth)
{
	FILE *fp = fopen("sample_pAndn.txt", "w");
	int splatDepth = 0;
	splatDepth = kernelDepth;
	for (int i = 0; i < samples.size();i++)
	{
		const ProjectiveData< OrientedPoint3D< Real >, Real >& sample = samples[i].sample;
		if (sample.weight > 0)
		{
			Point3D<Real> p = sample.data.p / sample.weight, n = sample.data.n;
			fprintf(fp, "%f %f %f %f %f %f %f\n",sample.weight, p[0], p[1], p[2], n[0], n[1], n[2]);
			if (!_InBounds(p)) { fprintf(stderr, "[WARNING] Octree:setNormalField: Point sample is out of bounds\n"); continue; }
			NonLinearSplatOrientedPoint(p, n, splatDepth, samplesPerNode, 1, maxDepth);
		}

	}
}
//***1
template<int Degree>
//int Octree<Degree>::setTree(OrientedPointStream< Real >& pointStream,std::vector< PointSample >& samples,char* fileName,const int& maxDepth,const int& binary,
//							const int& kernelDepth,const Real& samplesPerNode,const Real& scaleFactor,Point3D<Real>& center,Real& scale,
//							const int& resetSamples,const int& useConfidence){
//
int Octree<Degree>::setTree(std::vector< PointSample >& samples, char* fileName, const int& maxDepth, const int& binary,
	const int& kernelDepth, const Real& samplesPerNode, const Real& scaleFactor, Point3D<Real>& center, Real& scale,
	const int& resetSamples, const int& useConfidence) {

	Point3D<Real> min,max,position,normal,myCenter;
	Real myWidth;
	int i,cnt=0;
	TreeOctNode* temp;
	int splatDepth=0;
	FILE* fp;
	float c[2*DIMENSION];

	TreeNodeData::UseIndex=1;
	neighborKey.set(maxDepth);
	splatDepth=kernelDepth;
	if(splatDepth<0){splatDepth=0;}
	if(binary){fp=fopen(fileName,"rb");}
	else{fp=fopen(fileName,"r");}
	if(!fp){return 0;}

	DumpOutput("Setting bounding box\n");
	// Read through once to get the center and scale
	while(1){
		if(binary){if(fread(c,sizeof(float),2*DIMENSION,fp)!=6){break;}}
		else{if(fscanf(fp," %f %f %f %f %f %f ",&c[0],&c[1],&c[2],&c[3],&c[4],&c[5])!=2*DIMENSION){break;}}
		for(i=0;i<DIMENSION;i++){
			if(!cnt || c[i]<min.coords[i]){min.coords[i]=c[i];}
			if(!cnt || c[i]>max.coords[i]){max.coords[i]=c[i];}
		}
		cnt++;
	}
	for(i=0;i<DIMENSION;i++){
		if(!i || scale<max.coords[i]-min.coords[i]){scale=Real(max.coords[i]-min.coords[i]);}
		center.coords[i]=Real(max.coords[i]+min.coords[i])/2;
	}
	DumpOutput("Samples: %d\n",cnt);
	scale*=scaleFactor;
	for(i=0;i<DIMENSION;i++){center.coords[i]-=scale/2;}
	//**p0
	//if(splatDepth>0){
	//	DumpOutput("Setting sample weights\n");
	//	cnt=0;
	//	fseek(fp,SEEK_SET,0);
	//	while(1){
	//		if(binary){if(fread(c,sizeof(float),2*DIMENSION,fp)!=2*DIMENSION){break;}}
	//		else{if(fscanf(fp," %f %f %f %f %f %f ",&c[0],&c[1],&c[2],&c[3],&c[4],&c[5])!=2*DIMENSION){break;}}
	//		for(i=0;i<DIMENSION;i++){
	//			position.coords[i]=(c[i]-center.coords[i])/scale;
	//			normal.coords[i]=c[DIMENSION+i];
	//		}
	//		myCenter.coords[0]=myCenter.coords[1]=myCenter.coords[2]=Real(0.5);
	//		myWidth=Real(1.0);
	//		for(i=0;i<DIMENSION;i++){if(position.coords[i]<myCenter.coords[i]-myWidth/2 || position.coords[i]>myCenter.coords[i]+myWidth/2){break;}}
	//		if(i!=DIMENSION){continue;}
	//		temp=&tree;
	//		int d=0;
	//		Real weight=Real(1.0);
	//		if(useConfidence){weight=Real(Length(normal));}
	//		while(d<splatDepth){
	//			NonLinearUpdateWeightContribution(temp,position,weight);
	//			if(!temp->children){temp->initChildren();}
	//			int cIndex=TreeOctNode::CornerIndex(myCenter,position);
	//			temp=&temp->children[cIndex];
	//			myWidth/=2;
	//			if(cIndex&1){myCenter.coords[0]+=myWidth/2;}
	//			else		{myCenter.coords[0]-=myWidth/2;}
	//			if(cIndex&2){myCenter.coords[1]+=myWidth/2;}
	//			else		{myCenter.coords[1]-=myWidth/2;}
	//			if(cIndex&4){myCenter.coords[2]+=myWidth/2;}
	//			else		{myCenter.coords[2]-=myWidth/2;}
	//			d++;
	//		}
	//		NonLinearUpdateWeightContribution(temp,position,weight);
	//		cnt++;
	//	}
	//}
	//**p1
	DumpOutput("Adding Points and Normals\n");
	normals=new std::vector<Point3D<Real> >();
	ngbrs = new std::vector<std::vector<ngbrCoe<Real>>>();
	//std::vector<ngbrCoe<Real>> ngbr;
	std::vector< int > nodeToIndexMap;
	Point3D< Real > p, n;
	//***0
	//OrientedPoint3D< Real > _p;
	//***1
	cnt=0;
	fseek(fp,SEEK_SET,0);
	//**p0
	while (1) {
	//**p1

	//***0
	//while(pointStream.nextPoint(_p)){
		//***1
		//**p0
		if (binary) { if (fread(c, sizeof(float), 2 * DIMENSION, fp) != 2 * DIMENSION) { break; } }
		else { if (fscanf(fp, " %f %f %f %f %f %f ", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) != 2 * DIMENSION) { break; } }
		for (i = 0; i < DIMENSION; i++) {
			position.coords[i] = (c[i] - center.coords[i]) / scale;
			normal.coords[i] = c[DIMENSION + i];
		}
		myCenter.coords[0] = myCenter.coords[1] = myCenter.coords[2] = Real(0.5);
		myWidth = Real(1.0);
		for (i = 0; i < DIMENSION; i++) { if (position.coords[i]<myCenter.coords[i] - myWidth / 2 || position.coords[i]>myCenter.coords[i] + myWidth / 2) { break; } }
		if (i != DIMENSION) { continue; }

		//**p1

		//***0
		/*p = Point3D< Real >(_p.p), n = Point3D< Real >(_p.n);
		Point3D< Real > mycenter = Point3D< Real >(Real(0.5), Real(0.5), Real(0.5));
		Real mywidth = Real(1.0);*/
		//***1

		Real l = Real(Length(normal));
		if (l != l || l < EPSILON) { continue; }
		if (!useConfidence) {
			//**p0
			normal.coords[0] /= l;
			normal.coords[1] /= l;
			normal.coords[2] /= l;
			//**p1
			//***0
			//n /= l;
			//***1
		}
	//	/*l=Real(2<<maxDepth);
	//	normal.coords[0]*=l;
	//	normal.coords[1]*=l;
	//	normal.coords[2]*=l;*/
		for (int i = 0; i < DIMENSION; i++)
		{
			p.coords[i] = position.coords[i];
			n.coords[i] = normal.coords[i];
		}
		temp = &tree;
		
		int d = temp->depth();
		//printf("%f %d %d\n ", p[0],d,cnt);
		//std::cout << n[0] << " " << n[1] << " "<<n[2] << endl;
		while (d < maxDepth)
		{
			if (!temp->children) {
				//printf("nodeIndex of before %d \n", temp->indexforSample);
				temp->initChildren(_NodeInitializer);
				//printf("nodeIndex %d \n", temp->indexforSample);
			}
			int cIndex = TreeOctNode::CornerIndex(myCenter, p);
			temp = temp->children + cIndex;
			myWidth /= 2;
			if (cIndex & 1) { myCenter.coords[0] += myWidth / 2; }
			else { myCenter.coords[0] -= myWidth / 2; }
			if (cIndex & 2) { myCenter.coords[1] += myWidth / 2; }
			else { myCenter.coords[1] -= myWidth / 2; }
			if (cIndex & 4) { myCenter.coords[2] += myWidth / 2; }
			else { myCenter.coords[2] -= myWidth / 2; }
			d++;

		}
		Real weight = (Real)(useConfidence ? l : 1.);
		int nodeIndex = temp->indexforSample;
		if (nodeIndex >= nodeToIndexMap.size()) nodeToIndexMap.resize(nodeIndex + 1, -1);
		int idx = nodeToIndexMap[nodeIndex];
		//printf("nodeIndex %d %d\n", nodeIndex,  cnt);
		if (idx == -1)
		{
			//cout << nodeIndex << endl;
			idx = (int)samples.size();
			nodeToIndexMap[nodeIndex] = idx;
			samples.resize(idx + 1), samples[idx].node = temp;

		}
		//std::cout << n[0] << " " << n[1] << " " << n[2] <<" "<<weight<< endl;
		samples[idx].sample += ProjectiveData< OrientedPoint3D< Real >, Real >(OrientedPoint3D< Real >(p * weight, n * weight), weight);
		cnt++;
	}
		/*l=Real(2<<maxDepth);
		normal.coords[0]*=l;
		normal.coords[1]*=l;
		normal.coords[2]*=l;*/
		

	//	if(resetSamples && samplesPerNode>0 && splatDepth){
	//		//printf("%d %d %d \n", resetSamples, samplesPerNode, splatDepth);
	//		//printf("11\n");
	//		NonLinearSplatOrientedPoint(position,normal,splatDepth,samplesPerNode,1,maxDepth);
	//	}
	//	else{
	//		printf("11\n");
	//		Real alpha=1;
	//		temp=&tree;
	//		int d=0;
	//		if(splatDepth){
	//			while(d<splatDepth){
	//				int cIndex=TreeOctNode::CornerIndex(myCenter,position);
	//				temp=&temp->children[cIndex];
	//				myWidth/=2;
	//				if(cIndex&1){myCenter.coords[0]+=myWidth/2;}
	//				else		{myCenter.coords[0]-=myWidth/2;}
	//				if(cIndex&2){myCenter.coords[1]+=myWidth/2;}
	//				else		{myCenter.coords[1]-=myWidth/2;}
	//				if(cIndex&4){myCenter.coords[2]+=myWidth/2;}
	//				else		{myCenter.coords[2]-=myWidth/2;}
	//				d++;
	//			}
	//			alpha=NonLinearGetSampleWeight(temp,position);
	//		}
	//		for(i=0;i<DIMENSION;i++){normal.coords[i]*=alpha;}
	//		while(d<maxDepth){
	//			if(!temp->children){temp->initChildren();}
	//			int cIndex=TreeOctNode::CornerIndex(myCenter,position);
	//			temp=&temp->children[cIndex];
	//			myWidth/=2;
	//			if(cIndex&1){myCenter.coords[0]+=myWidth/2;}
	//			else		{myCenter.coords[0]-=myWidth/2;}
	//			if(cIndex&2){myCenter.coords[1]+=myWidth/2;}
	//			else		{myCenter.coords[1]-=myWidth/2;}
	//			if(cIndex&4){myCenter.coords[2]+=myWidth/2;}
	//			else		{myCenter.coords[2]-=myWidth/2;}
	//			d++;
	//		}
	//		NonLinearSplatOrientedPoint(temp,position,normal);
	//	}
	//}
	DumpOutput("Memory Usage: %.3f MB\n",float(MemoryUsage()));
	
	fclose(fp);
	printf("%d\n", cnt);
	return cnt;
}

template<int Degree>
void Octree<Degree>::setFunctionData(const PPolynomial<Degree>& ReconstructionFunction,	const int& maxDepth,const int& normalize,const Real& normalSmooth){

	radius=Real(fabs(ReconstructionFunction.polys[0].start));
	width=int(double(radius+0.5-EPSILON)*2);
	if(normalSmooth>0){postNormalSmooth=normalSmooth;}
	fData.set(maxDepth,ReconstructionFunction,normalize,1);
}

template<int Degree>
void Octree<Degree>::finalize1(const int& refineNeighbors)
{
	TreeOctNode* temp;

	if(refineNeighbors>=0){
		RefineFunction rf;
		temp=tree.nextNode();
		while(temp){
			if(temp->nodeData.nodeIndex>=0 && Length((*normals)[temp->nodeData.nodeIndex])>EPSILON){
				rf.depth=temp->depth()-refineNeighbors;
				TreeOctNode::ProcessMaxDepthNodeAdjacentNodes(fData.depth,temp,2*width,&tree,1,temp->depth()-refineNeighbors,&rf);
			}
			temp=tree.nextNode(temp);
		}
	}
	else if(refineNeighbors==-1234){
		temp=tree.nextLeaf();
		while(temp){
			if(!temp->children && temp->depth()<fData.depth){temp->initChildren();}
			temp=tree.nextLeaf(temp);
		}
	}
}
template<int Degree>
void Octree<Degree>::finalize2(const int& refineNeighbors)
{
	TreeOctNode* temp;

	if(refineNeighbors>=0){
		RefineFunction rf;
		temp=tree.nextNode();
		while(temp){
			if(fabs(temp->nodeData.value)>EPSILON){
				rf.depth=temp->depth()-refineNeighbors;
				TreeOctNode::ProcessMaxDepthNodeAdjacentNodes(fData.depth,temp,2*width,&tree,1,temp->depth()-refineNeighbors,&rf);
			}
			temp=tree.nextNode(temp);
		}
	}
}
template <int Degree>
Real Octree<Degree>::GetDivergence(const int idx[DIMENSION],const Point3D<Real>& normal) const
{
	double dot=fData.dotTable[idx[0]]*fData.dotTable[idx[1]]*fData.dotTable[idx[2]];
	return Real(dot*(fData.dDotTable[idx[0]]*normal.coords[0]+fData.dDotTable[idx[1]]*normal.coords[1]+fData.dDotTable[idx[2]]*normal.coords[2]));
}
template<int Degree>
Real Octree<Degree>::GetLaplacian(const int idx[DIMENSION]) const
{
	return Real(fData.dotTable[idx[0]]*fData.dotTable[idx[1]]*fData.dotTable[idx[2]]*(fData.d2DotTable[idx[0]]+fData.d2DotTable[idx[1]]+fData.d2DotTable[idx[2]]));
}
template<int Degree>
Real Octree<Degree>::GetDotProduct(const int idx[DIMENSION]) const
{
	return Real(fData.dotTable[idx[0]]*fData.dotTable[idx[1]]*fData.dotTable[idx[2]]);
}

template<int Degree>
int Octree<Degree>::GetFixedDepthLaplacian(SparseSymmetricMatrix<float>& matrix,const int& depth,const SortedTreeNodes& sNodes)
{
	LaplacianMatrixFunction mf;
	mf.ot=this;
	mf.offset=sNodes.nodeCount[depth];
	matrix.Resize(sNodes.nodeCount[depth+1]-sNodes.nodeCount[depth]);
	mf.rowElements=(MatrixEntry<float>*)malloc(sizeof(MatrixEntry<float>)*matrix.rows);
	for(int i=sNodes.nodeCount[depth];i<sNodes.nodeCount[depth+1];i++){
		mf.elementCount=0;
		mf.d2=int(sNodes.treeNodes[i]->d);
		mf.x2=int(sNodes.treeNodes[i]->off[0]);
		mf.y2=int(sNodes.treeNodes[i]->off[1]);
		mf.z2=int(sNodes.treeNodes[i]->off[2]);
		mf.index[0]=mf.x2;
		mf.index[1]=mf.y2;
		mf.index[2]=mf.z2;
		TreeOctNode::ProcessTerminatingNodeAdjacentNodes(fData.depth,sNodes.treeNodes[i],2*width-1,&tree,1,&mf);
		matrix.SetRowSize(i-sNodes.nodeCount[depth],mf.elementCount);
		memcpy(matrix.m_ppElements[i-sNodes.nodeCount[depth]],mf.rowElements,sizeof(MatrixEntry<float>)*mf.elementCount);
	}
	free(mf.rowElements);
	return 1;
}
template<int Degree>
int Octree<Degree>::GetRestrictedFixedDepthLaplacian(SparseSymmetricMatrix<float>& matrix,const int& depth,const int* entries,const int& entryCount,
													 const TreeOctNode* rNode,const Real& radius,
													 const SortedTreeNodes& sNodes){
	int i;
	RestrictedLaplacianMatrixFunction mf;
	Real myRadius=int(2*radius-ROUND_EPS)+ROUND_EPS;
	mf.ot=this;
	mf.radius=radius;
	rNode->depthAndOffset(mf.depth,mf.offset);
	matrix.Resize(entryCount);
	mf.rowElements=(MatrixEntry<float>*)malloc(sizeof(MatrixEntry<float>)*matrix.rows);
	for(i=0;i<entryCount;i++){sNodes.treeNodes[entries[i]]->nodeData.nodeIndex=i;}
	for(i=0;i<entryCount;i++){
		mf.elementCount=0;
		mf.index[0]=int(sNodes.treeNodes[entries[i]]->off[0]);
		mf.index[1]=int(sNodes.treeNodes[entries[i]]->off[1]);
		mf.index[2]=int(sNodes.treeNodes[entries[i]]->off[2]);
		TreeOctNode::ProcessTerminatingNodeAdjacentNodes(fData.depth,sNodes.treeNodes[entries[i]],2*width-1,&tree,1,&mf);
		matrix.SetRowSize(i,mf.elementCount);
		memcpy(matrix.m_ppElements[i],mf.rowElements,sizeof(MatrixEntry<float>)*mf.elementCount);
	}
	for(i=0;i<entryCount;i++){sNodes.treeNodes[entries[i]]->nodeData.nodeIndex=entries[i];}
	free(mf.rowElements);
	return 1;
}


template<int Degree>
int Octree<Degree>::LaplacianMatrixIteration(const int& subdivideDepth){
	int i,iter=0;
	SortedTreeNodes sNodes;
	double t;
	fData.setDotTables(fData.D2_DOT_FLAG);
	sNodes.set(tree,1);

	SparseMatrix<float>::SetAllocator(MEMORY_ALLOCATOR_BLOCK_SIZE);

	sNodes.treeNodes[0]->nodeData.value=0;
	for(i= 1;i<sNodes.maxDepth;i++){
	//for (i = sNodes.maxDepth-1; i<sNodes.maxDepth; i++) {
		DumpOutput("Depth: %d/%d\n",i,sNodes.maxDepth-1);
		t=Time();
		if(subdivideDepth>0){iter+=SolveFixedDepthMatrix(i,subdivideDepth,sNodes);}
		else{iter+=SolveFixedDepthMatrix(i,sNodes);}
	}
	SparseMatrix<float>::Allocator.reset();
	fData.clearDotTables(fData.DOT_FLAG | fData.D_DOT_FLAG | fData.D2_DOT_FLAG);
	return iter;
}

template<int Degree>
int Octree<Degree>::SolveFixedDepthMatrix(const int& depth,const SortedTreeNodes& sNodes){
	int i,iter=0;
	Vector<double> V,Solution;
	SparseSymmetricMatrix<Real> matrix;
	Real myRadius;
	double gTime,sTime,uTime;
	Real dx,dy,dz;
	int x1,x2,y1,y2,z1,z2;
	Vector<Real> Diagonal;

	gTime=Time();
	V.Resize(sNodes.nodeCount[depth+1]-sNodes.nodeCount[depth]);
	
	for(i=sNodes.nodeCount[depth];i<sNodes.nodeCount[depth+1];i++){V[i-sNodes.nodeCount[depth]]=sNodes.treeNodes[i]->nodeData.value;}
	FILE *fp = NULL;
	fp = fopen("b.txt", "w");
	printf("Depth's nodeCount is %d and (Depth+1)'s nodeCount is %d\n",sNodes.nodeCount[depth], sNodes.nodeCount[depth + 1]);
	int num_b = 0;
	int num_nodeData = 0;
	for (i = sNodes.nodeCount[depth]; i<sNodes.nodeCount[depth + 1]; i++) 
	{ 
		//V[i - sNodes.nodeCount[depth]] = sNodes.treeNodes[i]->b;

		if (depth==sNodes.maxDepth-1)
		{
			if (fabs(sNodes.treeNodes[i]->b))
			{
				num_b++;
			}
			if (fabs(sNodes.treeNodes[i]->nodeData.value))
			{
				num_nodeData++;
			}
			fprintf(fp, "depth %d sNodes.treeNodes[i]->b is %f and nodeData.value is %f\n", depth,sNodes.treeNodes[i]->b, sNodes.treeNodes[i]->nodeData.value);
		}
	}
	printf("num_b is %d and num_nodeData is %d\n", num_b, num_nodeData);
	SparseSymmetricMatrix<float>::Allocator.rollBack();
	GetFixedDepthLaplacian(matrix,depth,sNodes);
	gTime=Time()-gTime;
	DumpOutput("\tMatrix entries: %d / %d^2 = %.4f%%\n",matrix.Entries(),matrix.rows,100.0*(matrix.Entries()/double(matrix.rows))/matrix.rows);
	DumpOutput("\tMemory Usage: %.3f MB\n",float(MemoryUsage()));
	sTime=Time();
	iter+=SparseSymmetricMatrix<Real>::Solve(matrix,V,int(pow(matrix.rows,ITERATION_POWER)),Solution,double(EPSILON),1);
	sTime=Time()-sTime;
	uTime=Time();
	for (int i = sNodes.nodeCount[depth]; i < sNodes.nodeCount[depth + 1]; i++)
	{
		fprintf(fp, "%f\n", Real(Solution[i - sNodes.nodeCount[depth]]));
	}
	for(i=sNodes.nodeCount[depth];i<sNodes.nodeCount[depth+1];i++){sNodes.treeNodes[i]->nodeData.value=Real(Solution[i-sNodes.nodeCount[depth]]);}
	//for (i = sNodes.nodeCount[depth]; i<sNodes.nodeCount[depth + 1]; i++) { sNodes.treeNodes[i]->b = Real(Solution[i - sNodes.nodeCount[depth]]); }
	myRadius=Real(radius+ROUND_EPS-0.5);
	myRadius /=(1<<depth);

	if(depth<sNodes.maxDepth-1){
		LaplacianProjectionFunction pf;
		TreeOctNode *node1,*node2;
		pf.ot=this;
		int idx1,idx2,off=sNodes.nodeCount[depth];
		// First pass: idx2 is the solution coefficient propogated
		for(i=0;i<matrix.rows;i++){
			idx1=i;
			node1=sNodes.treeNodes[idx1+off];
			if(!node1->children){continue;}
			x1=int(node1->off[0]);
			y1=int(node1->off[1]);
			z1=int(node1->off[2]);
			for(int j=0;j<matrix.rowSizes[i];j++){
				idx2=matrix.m_ppElements[i][j].N;
				node2=sNodes.treeNodes[idx2+off];
				x2=int(node2->off[0]);
				y2=int(node2->off[1]);
				z2=int(node2->off[2]);
				pf.value=Solution[idx2];
				pf.index[0]=x2;
				pf.index[1]=y2;
				pf.index[2]=z2;
				dx=Real(x2-x1)/(1<<depth);
				dy=Real(y2-y1)/(1<<depth);
				dz=Real(z2-z1)/(1<<depth);
				if(fabs(dx)<myRadius && fabs(dy)<myRadius && fabs(dz)<myRadius){node1->processNodeNodes(node2,&pf,0);}
				else{TreeOctNode::ProcessNodeAdjacentNodes(fData.depth,node2,width,node1,width,&pf,0);}
			}
		}
		// Second pass: idx1 is the solution coefficient propogated
		for(i=0;i<matrix.rows;i++){
			idx1=i;
			node1=sNodes.treeNodes[idx1+off];
			x1=int(node1->off[0]);
			y1=int(node1->off[1]);
			z1=int(node1->off[2]);
			pf.value=Solution[idx1];
			pf.index[0]=x1;
			pf.index[1]=y1;
			pf.index[2]=z1;
			for(int j=0;j<matrix.rowSizes[i];j++){
				idx2=matrix.m_ppElements[i][j].N;
				node2=sNodes.treeNodes[idx2+off];
				if(idx1!=idx2 && node2->children){
					x2=int(node2->off[0]);
					y2=int(node2->off[1]);
					z2=int(node2->off[2]);
					dx=Real(x1-x2)/(1<<depth);
					dy=Real(y1-y2)/(1<<depth);
					dz=Real(z1-z2)/(1<<depth);
					if(fabs(dx)<myRadius && fabs(dy)<myRadius && fabs(dz)<myRadius){node2->processNodeNodes(node1,&pf,0);}
					else{TreeOctNode::ProcessNodeAdjacentNodes(fData.depth,node1,width,node2,width,&pf,0);}
				}
			}
		}
	}
	uTime=Time()-uTime;
	DumpOutput("\tGot / Solved / Updated in: %6.3f / %6.3f / %6.3f\n",gTime,sTime,uTime);
	return iter;
}
template<int Degree>
int Octree<Degree>::SolveFixedDepthMatrix(const int& depth,const int& startingDepth,const SortedTreeNodes& sNodes){
	int i,j,d,iter=0;
	SparseSymmetricMatrix<Real> matrix;
	AdjacencySetFunction asf;
	AdjacencyCountFunction acf;
	Vector<Real> Values;
	Vector<double> SubValues,SubSolution;
	double gTime,sTime,uTime;
	Real myRadius,myRadius2;
	Real dx,dy,dz;
	Vector<Real> Diagonal;

	if(startingDepth>=depth){return SolveFixedDepthMatrix(depth,sNodes);}

	Values.Resize(sNodes.nodeCount[depth+1]-sNodes.nodeCount[depth]);

	for(i=sNodes.nodeCount[depth];i<sNodes.nodeCount[depth+1];i++){
		Values[i-sNodes.nodeCount[depth]]=sNodes.treeNodes[i]->nodeData.value;
		sNodes.treeNodes[i]->nodeData.value=0;
	}

	myRadius=2*radius-Real(0.5);
	myRadius=int(myRadius-ROUND_EPS)+ROUND_EPS;
	myRadius2=Real(radius+ROUND_EPS-0.5);
	d=depth-startingDepth;
	for(i=sNodes.nodeCount[d];i<sNodes.nodeCount[d+1];i++){
		gTime=Time();
		TreeOctNode* temp;
		// Get all of the entries associated to the subspace
		acf.adjacencyCount=0;
		temp=sNodes.treeNodes[i]->nextNode();
		while(temp){
			if(temp->depth()==depth){
				acf.Function(temp,temp);
				temp=sNodes.treeNodes[i]->nextBranch(temp);
			}
			else{temp=sNodes.treeNodes[i]->nextNode(temp);}
		}
		for(j=sNodes.nodeCount[d];j<sNodes.nodeCount[d+1];j++){
			if(i==j){continue;}
			TreeOctNode::ProcessFixedDepthNodeAdjacentNodes(fData.depth,sNodes.treeNodes[i],1,sNodes.treeNodes[j],2*width-1,depth,&acf);
		}
		if(!acf.adjacencyCount){continue;}
		asf.adjacencies=new int[acf.adjacencyCount];
		asf.adjacencyCount=0;
		temp=sNodes.treeNodes[i]->nextNode();
		while(temp){
			if(temp->depth()==depth){
				asf.Function(temp,temp);
				temp=sNodes.treeNodes[i]->nextBranch(temp);
			}
			else{temp=sNodes.treeNodes[i]->nextNode(temp);}
		}
		for(j=sNodes.nodeCount[d];j<sNodes.nodeCount[d+1];j++){
			if(i==j){continue;}
			TreeOctNode::ProcessFixedDepthNodeAdjacentNodes(fData.depth,sNodes.treeNodes[i],1,sNodes.treeNodes[j],2*width-1,depth,&asf);
		}

		DumpOutput("\tNodes[%d/%d]: %d\n",i-sNodes.nodeCount[d]+1,sNodes.nodeCount[d+1]-sNodes.nodeCount[d],asf.adjacencyCount);
		// Get the associated vector
		SubValues.Resize(asf.adjacencyCount);
		for(j=0;j<asf.adjacencyCount;j++){SubValues[j]=Values[asf.adjacencies[j]-sNodes.nodeCount[depth]];}
		SubSolution.Resize(asf.adjacencyCount);
		for(j=0;j<asf.adjacencyCount;j++){SubSolution[j]=sNodes.treeNodes[asf.adjacencies[j]]->nodeData.value;}
		// Get the associated matrix
		SparseSymmetricMatrix<float>::Allocator.rollBack();
		GetRestrictedFixedDepthLaplacian(matrix,depth,asf.adjacencies,asf.adjacencyCount,sNodes.treeNodes[i],myRadius,sNodes);
		gTime=Time()-gTime;
		DumpOutput("\t\tMatrix entries: %d / %d^2 = %.4f%%\n",matrix.Entries(),matrix.rows,100.0*(matrix.Entries()/double(matrix.rows))/matrix.rows);
		DumpOutput("\t\tMemory Usage: %.3f MB\n",float(MemoryUsage()));

		// Solve the matrix
		sTime=Time();
		iter+=SparseSymmetricMatrix<Real>::Solve(matrix,SubValues,int(pow(matrix.rows,ITERATION_POWER)),SubSolution,double(EPSILON),0);
		sTime=Time()-sTime;

		uTime=Time();
		LaplacianProjectionFunction lpf;
		lpf.ot=this;

		// Update the solution for all nodes in the sub-tree
		for(j=0;j<asf.adjacencyCount;j++){
			temp=sNodes.treeNodes[asf.adjacencies[j]];
			while(temp->depth()>sNodes.treeNodes[i]->depth()){temp=temp->parent;}
			if(temp->nodeData.nodeIndex>=sNodes.treeNodes[i]->nodeData.nodeIndex){sNodes.treeNodes[asf.adjacencies[j]]->nodeData.value=Real(SubSolution[j]);}
		}
		double t=Time();
		// Update the values in the next depth
		int x1,x2,y1,y2,z1,z2;
		if(depth<sNodes.maxDepth-1){
			int idx1,idx2;
			TreeOctNode *node1,*node2;
			// First pass: idx2 is the solution coefficient propogated
			for(j=0;j<matrix.rows;j++){
				idx1=asf.adjacencies[j];
				node1=sNodes.treeNodes[idx1];
				if(!node1->children){continue;}
				x1=int(node1->off[0]);
				y1=int(node1->off[1]);
				z1=int(node1->off[2]);

				for(int k=0;k<matrix.rowSizes[j];k++){
					idx2=asf.adjacencies[matrix.m_ppElements[j][k].N];
					node2=sNodes.treeNodes[idx2];
					temp=node2;
					while(temp->depth()>d){temp=temp->parent;}
					if(temp!=sNodes.treeNodes[i]){continue;}
					lpf.value=Real(SubSolution[matrix.m_ppElements[j][k].N]);
					x2=int(node2->off[0]);
					y2=int(node2->off[1]);
					z2=int(node2->off[2]);
					lpf.index[0]=x2;
					lpf.index[1]=y2;
					lpf.index[2]=z2;
					dx=Real(x2-x1)/(1<<depth);
					dy=Real(y2-y1)/(1<<depth);
					dz=Real(z2-z1)/(1<<depth);
					if(fabs(dx)<myRadius2 && fabs(dy)<myRadius2 && fabs(dz)<myRadius2){node1->processNodeNodes(node2,&lpf,0);}
					else{TreeOctNode::ProcessNodeAdjacentNodes(fData.depth,node2,width,node1,width,&lpf,0);}
				}
			}
			// Second pass: idx1 is the solution coefficient propogated
			for(j=0;j<matrix.rows;j++){
				idx1=asf.adjacencies[j];
				node1=sNodes.treeNodes[idx1];
				temp=node1;
				while(temp->depth()>d){temp=temp->parent;}
				if(temp!=sNodes.treeNodes[i]){continue;}
				x1=int(node1->off[0]);
				y1=int(node1->off[1]);
				z1=int(node1->off[2]);

				lpf.value=Real(SubSolution[j]);
				lpf.index[0]=x1;
				lpf.index[1]=y1;
				lpf.index[2]=z1;
				for(int k=0;k<matrix.rowSizes[j];k++){
					idx2=asf.adjacencies[matrix.m_ppElements[j][k].N];
					node2=sNodes.treeNodes[idx2];
					if(!node2->children){continue;}

					if(idx1!=idx2){
						x2=int(node2->off[0]);
						y2=int(node2->off[1]);
						z2=int(node2->off[2]);
						dx=Real(x1-x2)/(1<<depth);
						dy=Real(y1-y2)/(1<<depth);
						dz=Real(z1-z2)/(1<<depth);
						if(fabs(dx)<myRadius2 && fabs(dy)<myRadius2 && fabs(dz)<myRadius2){node2->processNodeNodes(node1,&lpf,0);}
						else{TreeOctNode::ProcessNodeAdjacentNodes(fData.depth,node1,width,node2,width,&lpf,0);}
					}
				}
			}
		}
		uTime=Time()-uTime;
		DumpOutput("\t\tGot / Solved / Updated in: %6.3f / %6.3f / %6.3f\n",gTime,sTime,uTime);
		delete[] asf.adjacencies;
	}
	return iter;
}
template<int Degree>
int Octree<Degree>::HasNormals(TreeOctNode* node,const Real& epsilon){
	int hasNormals=0;
	if(node->nodeData.nodeIndex>=0 && Length((*normals)[node->nodeData.nodeIndex])>epsilon){hasNormals=1;}
	if(node->children){for(int i=0;i<Cube::CORNERS && !hasNormals;i++){hasNormals|=HasNormals(&node->children[i],epsilon);}}

	return hasNormals;
}
template<int Degree>
void Octree<Degree>::ClipTree(void){
	TreeOctNode* temp;

	temp=tree.nextNode();

	while(temp){
		if(temp->children){
			int hasNormals=0;
			for(int i=0;i<Cube::CORNERS && !hasNormals;i++){hasNormals=HasNormals(&temp->children[i],EPSILON);}
			if(!hasNormals){temp->children=NULL;}
		}
		temp=tree.nextNode(temp);
	}

}
template<int Degree>
void Octree<Degree>::SetLaplacianWeights(void){
	TreeOctNode* temp;

	fData.setDotTables(fData.DOT_FLAG | fData.D_DOT_FLAG);
	DivergenceFunction df;
	df.ot=this;
	temp=tree.nextNode();
	while(temp){
		if(temp->nodeData.nodeIndex<0 || Length((*normals)[temp->nodeData.nodeIndex])<=EPSILON){
			temp=tree.nextNode(temp);
			continue;
		}
		int d=temp->depth();
		df.normal=(*normals)[temp->nodeData.nodeIndex];
		// printf("%d \n", d);
		//printf("%f %f %f\n", df.normal.coords[0], df.normal.coords[1], df.normal.coords[2]);
		df.index[0]=int(temp->off[0]);
		df.index[1]=int(temp->off[1]);
		df.index[2]=int(temp->off[2]);
		TreeOctNode::ProcessNodeAdjacentNodes(fData.depth,temp,width,&tree,width,&df);
		temp=tree.nextNode(temp);
	}
	fData.clearDotTables(fData.D_DOT_FLAG);
	temp=tree.nextNode();
	while(temp){
		if(temp->nodeData.nodeIndex<0){temp->nodeData.centerWeightContribution=0;}
		else{temp->nodeData.centerWeightContribution=Real(Length((*normals)[temp->nodeData.nodeIndex]));}
		temp=tree.nextNode(temp);
	}
	MemoryUsage();

	delete normals;
	normals=NULL;
}

template<int Degree>
void Octree<Degree>::getTreeSize(void)
{
	TreeOctNode *temp;
	int num = 0;
	temp = tree.nextNode();
	while (temp)
	{
		temp = tree.nextNode(temp);
		num++;
	}
	printf("size of tree %d\n", num);
}
template<int Degree>
void Octree<Degree>::GreenMethod(void)
{
	FILE *fp_V = fopen("V3_0.txt", "w");

	for (int i = 0; i<normals->size(); i++)
	{
		Point3D< Real > fpV = (*normals)[i];
		fprintf(fp_V, "%f %f %f\n", fpV[0], fpV[1], fpV[2]);
	}
	fclose(fp_V);
	int ngbrs_size = ngbrs->size();
	int normals_size = normals->size();
	//int maxindex = 0;
	//for (int i = 0; i < ngbrs->size(); i++)
	//{
	//	std::vector<ngbrCoe<Real>> a=(*ngbrs)[i];
	//	for (int j = 0; j < a.size(); j++)
	//	{
	//		int k = a[j].index;
	//		if (k > maxindex)
	//		{
	//			maxindex = k;
	//		}
	//	}
	//}
	//printf("ngbrs size %d  normals size %d maxindex %d\n", ngbrs->size(),normals->size(),maxindex);



	//计算散度
	int size_sample = (*normals).size();
	int size_normal = size_sample * 4;
	int size_col = 300;
	//声明法向量散度的值
	double *divergence_normal_Value = NULL;
	divergence_normal_Value = (double *)malloc(size_normal * size_col * 3 * sizeof(double));
	if (divergence_normal_Value == NULL)
	{
		printf("divergence_normal_Value malloc failed\n");
	}
	memset(divergence_normal_Value, 0, size_normal * size_col * 3 * sizeof(double));
	printf("compute divergence\n");
	//声明法向量散度对应的sample的索引
	int *divergence_normal_SampleIndex = NULL;
	divergence_normal_SampleIndex = (int *)malloc(size_normal * size_col * 3 * sizeof(int));
	if (divergence_normal_SampleIndex == NULL)
	{
		printf("divergence_normla_SampleIndex malloc failed\n");
	}
	memset(divergence_normal_SampleIndex, 0, size_normal * size_col * 3 * sizeof(int));

	//声明每一个法向量散度值的长度
	int *divergence_normal_EachLength = NULL;
	divergence_normal_EachLength = (int *)malloc(size_normal * sizeof(int));
	if (divergence_normal_EachLength == NULL)
	{
		printf("divergence_normal_EachLength malloc failed\n");
	}
	memset(divergence_normal_EachLength, 0, size_normal * sizeof(int));
	//sizeOfDivergence 记录实际产生divergence域的容量
	int *sizeOfDivergence = NULL;
	sizeOfDivergence = (int *)malloc(sizeof(int));
	if (sizeOfDivergence == NULL)
	{
		printf("sizeOfDivergence malloc failed\n");
	}
	*sizeOfDivergence = 0;
	TreeOctNode* temp;
	MyDivergenceFuntion mf;
	Eigen::VectorXd Tnormal(normals_size * 3);
	for (int i = 0; i < normals_size * 3; i++)
	{
		Tnormal[i] = 0;
	}
	temp = tree.nextNode();

	//输出样本*8的区域到V.txt

	//printf("size of tree %d\n", Tree.Size);
	temp = tree.nextNode();
	int num = 0;
	while (temp)
	{
		if (temp->nodeData.nodeIndex<0 || Length((*normals)[temp->nodeData.nodeIndex]) <= EPSILON) {
			temp = tree.nextNode(temp);
			continue;
		}
		mf.ot = this;
		mf.index[0] = int(temp->off[0]);
		mf.index[1] = int(temp->off[1]);
		mf.index[2] = int(temp->off[2]);
		mf.nodeIndex = temp->nodeData.nodeIndex;
		Tnormal[temp->nodeData.nodeIndex * 3] = (*normals)[temp->nodeData.nodeIndex].coords[0];
		Tnormal[temp->nodeData.nodeIndex * 3 + 1] = (*normals)[temp->nodeData.nodeIndex].coords[1];
		Tnormal[temp->nodeData.nodeIndex * 3 + 2] = (*normals)[temp->nodeData.nodeIndex].coords[2];
		TreeOctNode::ProcessNodeAdjacentNodesTocomputeMydivergence(fData.depth, temp, width, &tree, width, &mf,divergence_normal_Value,divergence_normal_SampleIndex,divergence_normal_EachLength,sizeOfDivergence,size_col*3);
		temp = tree.nextNode(temp);
		num++;
	}
	printf("*sizeOfDivergence %d num %d normals_size %d \n", *sizeOfDivergence,num,normals->size()); 
	int maxLen = 0;
	FILE *fp_Div = fopen("Divergence.txt", "w");
	for (int i = 0; i < *sizeOfDivergence; i++)
	{
		if (*(divergence_normal_EachLength + i) > maxLen)
		{
			maxLen = *(divergence_normal_EachLength + i);
		}
		for (int j = 0; j < *(divergence_normal_EachLength + i); j++)
		{
			fprintf(fp_Div, "%f ", *(divergence_normal_Value + i*size_col * 3 + j));
		}
		fprintf(fp_Div, "\n");
	}
	fclose(fp_Div);
	printf("maxLen of divergence %d\n", maxLen);
	//typedef Eigen::Triplet<double> TStoO;
	////s->o (F(o,s)) s为行数
	//Eigen::SparseMatrix<double> matStoO(normals_size *3, ngbrs_size *3 );
	//std::vector<TStoO> coeStoO;

	//for (int i = 0; i < ngbrs_size; i++)
	//{
	//	std::vector<ngbrCoe<Real>> ngbr = (*ngbrs)[i];
	//	for (int j = 0; j < ngbr.size(); j++)
	//	{
	//		//o 在normals 中的索引
	//		int indexOfO = ngbr[j].index;
	//		//Coe 为F(o,s)
	//		int Coe = ngbr[j].value;
	//		coeStoO.push_back(TStoO(indexOfO * 3, i * 3, Coe));
	//		coeStoO.push_back(TStoO(indexOfO * 3 + 1, i * 3 + 1, Coe));
	//		coeStoO.push_back(TStoO(indexOfO * 3 + 1, i * 3 + 1, Coe));
	//	}
	//}
	//matStoO.setFromTriplets(coeStoO.begin(), coeStoO.end());
	//
	delete ngbrs;
	ngbrs = NULL;
	

	//计算f(x)和f(x)''(二阶导)
	double * coefficient = NULL;
	coefficient = (double *)malloc(size_normal * 7 * size_col * 3 * sizeof(double));
	if (coefficient == NULL)
	{
		printf("coefficient malloc failed\n");
	}
	memset(coefficient, 0, size_normal * 7 * size_col * 3 * sizeof(double));

	//系数矩阵中每一行对应样本的索引
	int * coefficient_SampleIndex = NULL;
	coefficient_SampleIndex = (int *)malloc(size_normal * 7 * size_col * 3 * sizeof(int));
	if (coefficient_SampleIndex == NULL)
	{
		printf("coefficient_SampleIndex malloc failed\n");
	}
	memset(coefficient_SampleIndex, 0, size_normal * 7 * size_col * 3 * sizeof(int));

	//系数矩阵中每一行值不为0的数量
	int * coefficient_EachLength = NULL;
	coefficient_EachLength = (int *)malloc(size_normal * 7 * sizeof(int));
	if (coefficient_EachLength == NULL)
	{
		printf("coefficient_EachLengh malloc failed\n");
	}
	memset(coefficient_EachLength, 0, size_normal * 7 * sizeof(int));
	
	int *sizeOfcoefficient = NULL;
	sizeOfcoefficient = (int *)malloc(sizeof(int));
	if (sizeOfcoefficient == NULL)
	{
		printf("sizeOfcoefficient malloc failed\n");
	}
	*sizeOfcoefficient = 0;


	//
	SortedTreeNodes sNodes;
	
	sNodes.set(tree, 1);

	GreenFunction gf;
	temp = tree.nextNode();
	//int num = 0;
	num=0;
	while (temp)
	{
		if (temp->indexOfdivergence == -1) {
			temp = tree.nextNode(temp);
			continue;
		}
		Point3D<Real> center2 = Point3D<Real>{ 0,0,0 };
		Real width2=0;
		temp->centerAndWidth(center2, width2);
		gf.center2[0] = center2.coords[0];
		gf.center2[1] = center2.coords[1];
		gf.center2[2] = center2.coords[2];
		gf.width2 = width2;
		TreeOctNode::ProcessNodeAdjacentNodesTocomputeMyFunctionByGreen(fData.depth, temp, width, &tree, width, &gf, divergence_normal_Value, divergence_normal_SampleIndex, divergence_normal_EachLength, coefficient, coefficient_SampleIndex, coefficient_EachLength, sizeOfcoefficient, size_col * 3);
		temp = tree.nextNode(temp);
		num++;
	}
	printf("num of divergence %d  sizeOfcoefficient %d\n",num,*sizeOfcoefficient);
	
	FILE *fp_Green = fopen("Green.txt", "w");
	maxLen = 0;
	for (int i = 0; i < *sizeOfcoefficient; i++)
	{
		for (int k = 0; k < 7; k++)
		{
			//printf("%d\n", *(coefficient_EachLength + 7 * i + k));
			if (*(coefficient_EachLength + i * 7 + k) > maxLen)
			{
				maxLen = *(coefficient_EachLength + i * 7 + k);
			}
			for (int j = 0; j < *(coefficient_EachLength + i*7+k); j++)
			{
				fprintf(fp_Green, "%f ", *(coefficient + (i*7+k)*size_col * 3 + j));
			}
			fprintf(fp_Green, "\n");
		}

	}
	fclose(fp_Green);
	printf("maxLen of Green is %d\n", maxLen);


	typedef Eigen::Triplet<double> T;
	Eigen::SparseMatrix<double> mat1(*sizeOfcoefficient * 7, normals_size * 3);
	Eigen::SparseMatrix<double> mat2(normals_size * 3, *sizeOfcoefficient * 7);
	Eigen::SparseMatrix<double> mat3(normals_size * 3, normals_size * 3);
	//Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<double>> solver;
	//Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> solver;
	Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
	std::vector<T> coeffi;

	for (int i = 0; i < *sizeOfcoefficient; i++)
	{
		{
			for (int k = 0; k < 7; k++)
			{
				for (int j = 0; j < *(coefficient_EachLength + i * 7 + k); j++)
				{
					//cout << nodeIndexInSpare << " " << *(coefficient_SampleIndex + nodeIndexInSpare*size_col*3 + j) << endl;
					coeffi.push_back(T(i * 7 + k, *(coefficient_SampleIndex + (i * 7 + k)*size_col * 3 + j), *(coefficient + (i * 7 + k)*size_col * 3 + j)));
					//cout << i << " " << *(coefficient_SampleIndex + i*size_col*3 + j) << " " << *(coefficient + i*size_col*3 + j) << ;
				}

			}

		}

	}
	free(coefficient);
	free(coefficient_EachLength);
	free(coefficient_SampleIndex);
	mat1.setFromTriplets(coeffi.begin(), coeffi.end());
	cout << "coefficient has inited" << endl;
	mat2 = mat1.transpose();
	//mat2 = mat1.adjoint();
	mat3 = mat2*mat1;
	//for (int i = 0; i < 100; i++)
	//{
	//	for (int j = 0; j < 10; j++)
	//	{
	//		printf("%f ", mat3.coeff(i, j));
	//	}
	//	printf("\n");
	//}
	Eigen::VectorXd b(*sizeOfcoefficient * 7), ATb(normals_size * 3), test(normals_size * 3);


	//因为值都为0.5，因此不用再算一遍nodeIndexInSpare
	for (int i = 0; i < *sizeOfcoefficient * 7; i++)
	{
		if (i % 7 == 0)
		{
			b[i] = 0.5;
		}
		else
		{
			b[i] = 0;
		}

	}
	ATb = mat2*b;

	cout << "start solve equation" << endl;
	Eigen::VectorXd x;

	x = solver.compute(mat3).solve(ATb);
	cout << "equation has solved" << endl;

	Eigen::VectorXd fx_true = mat1*x;
	//Eigen::VectorXd fx_right = mat1*x;
	Eigen::VectorXd fx_right = mat1*Tnormal;
	

	FILE *fp_result = fopen("result.txt", "w");
	for (int i = 0; i < normals_size; i++)
	{
		double len = sqrt(x[i * 3] * x[i * 3] + x[i * 3 + 1] * x[i * 3 + 1] + x[i * 3 + 2] * x[i * 3 + 2]);
		fprintf(fp_result, "%f %f %f %f %f %f %f %f %f %f %f %f\n", Tnormal[3 * i], Tnormal[3 * i + 1], Tnormal[3 * i + 2], x[i * 3], x[i * 3 + 1], x[i * 3 + 2], x[i * 3] / len, x[i * 3 + 1] / len, x[i * 3 + 2] / len,ATb[i*3],ATb[i*3+1],ATb[i*3+2]);
		//fprintf(fp, "%f %f %f\n", x[i * 3], x[i * 3 + 1], x[i * 3 + 2]);

	}
	FILE *fp_test = fopen("test.txt", "w");
	Eigen::VectorXd fx2 = mat3*x;
	test = mat3*x - ATb;
	for (int i = 0; i < size_sample * 3; i++)
	{
		fprintf(fp_test, "%f %f %f\n", fx2[i], ATb[i], test[i]);
	}

	double norm_test = 0;
	double norm_b = 0;
	for (int i = 0; i < size_sample * 3; i++)
	{
		norm_test += test[i] * test[i];
		norm_b += ATb[i] * ATb[i];
	}
	cout << "error is " << sqrt(norm_test) / sqrt(norm_b) << endl;

	fclose(fp_test);



	delete normals;
	normals = NULL;
	free(divergence_normal_Value);
	divergence_normal_Value = NULL;
	free(divergence_normal_SampleIndex);
	divergence_normal_SampleIndex = NULL;
	free(divergence_normal_EachLength);
	divergence_normal_EachLength = NULL;

	//free(coefficient);
	//coefficient = NULL;
	//free(coefficient_SampleIndex);
	//coefficient_SampleIndex = NULL;
	//free(coefficient_EachLength);
	//coefficient_EachLength = NULL;



	FILE *fp_right = fopen("fx_right.txt", "w");

	for (int i = 0;i < fx_right.size(); i++)
	{
		//fprintf(fp_right, "%f %f %f\n",fx_true, fx_right[i],b[i]);
		fprintf(fp_right, "%f\n",fx_right[i]);
	}
	fclose(fp_right);


	//MC实现
	printf("MC start\n");
	//std::cout << "MC start" << std::endl;
	float xmin = 100;
	float xmax = -100;
	float ymin = 100;
	float ymax = -100;
	float zmin = 100;
	float zmax = -100;
	float width = 0;



	//寻找坐标原点
	temp = tree.nextNode();
	num = 0;
	while (temp)
	{
		if (temp->indexOfcoefficient == -1) {
			temp = tree.nextNode(temp);
			continue;
		}
		Point3D<Real> center2 = Point3D<Real>{ 0,0,0 };
		Real width2 = 0;
		temp->centerAndWidth(center2, width2);
		width = width2;
		float x = center2.coords[0];
		float y = center2.coords[1];
		float z = center2.coords[2];
		if (x < xmin)
		{
			xmin = x;
		}
		if (x > xmax)
		{
			xmax = x;
		}
		if (y < ymin)
		{
			ymin = y;
		}
		if (y > ymax)
		{
			ymax = y;
		}
		if (z < zmin)
		{
			zmin = z;
		}
		if (z > zmax)
		{
			zmax = z;
		}
		temp = tree.nextNode(temp);
		num++;
	}
	//printf("xim %f xmax %f ymin %f ymax %f zmin %f zmax %f\n", xmin, xmax, ymin, ymax, zmin, zmax);
	//cout << xmin << " " << xmax << " " << ymin << " " << ymax << " " << zmin << " " << zmax << endl;
	//printf("xmax-xmin/width %f (ymax-ymin)/width %f (zmax-zmin)/width %f\n", (xmax - xmin) / width, (ymax - ymin) / width, (zmax - zmin) / width);
	//cout << (xmax - xmin) / width << " " << (ymax - ymin) / width << " " << (zmax - zmin) / width << endl;
	int size_x = (xmax - xmin) / width;
	int size_y = (ymax - ymin) / width;
	int size_z = (zmax - zmin) / width;

	//将数据变为整型索引存储,value表示fx_right
	float *tranigles = NULL;
	tranigles = (float *)malloc((size_x + 1)*(size_y + 1)*(size_z + 1) * sizeof(float));
	if (NULL == tranigles)
	{
		printf("tranigles malloc failed\n");
	}
	memset(tranigles, 0, (size_x + 1)*(size_y + 1)*(size_z + 1) * sizeof(float));


	temp = tree.nextNode();
	num = 0;
	while (temp)
	{
		if (temp->indexOfcoefficient == -1) {
			temp = tree.nextNode(temp);
			continue;
		}
		Point3D<Real> center2 = Point3D<Real>{ 0,0,0 };
		Real width2 = 0;
		temp->centerAndWidth(center2, width2);
		width = width2;
		int x = (center2.coords[0] - xmin) / width;
		int y = (center2.coords[1] - ymin) / width;
		int z = (center2.coords[2] - zmin) / width;
		tranigles[x*size_y*size_z + y*size_z + z] = fx_right[temp->indexOfcoefficient * 7];
		temp = tree.nextNode(temp);
		num++;
	}

	printf("init data\n");
	//std::cout << "init data" << std::endl;
	//MC前数据初始化
	int sizeSolution = *sizeOfcoefficient;
	int indexOfCube = 0;
	float *mcCubeValue = NULL;
	mcCubeValue = (float *)malloc(sizeSolution * 8 * sizeof(float));
	if (mcCubeValue == NULL)
	{
		printf("mcCubeValue malloc failed\n");
	}
	memset(mcCubeValue, 0, sizeSolution * 8 * sizeof(float));
	//std::cout << sizeSolution << std::endl;
	printf("%d\n", sizeSolution);



	float* mcCubePosition = NULL;
	mcCubePosition = (float *)malloc(sizeSolution * 4 * sizeof(float));
	if (mcCubePosition == NULL)
	{
		printf("mcCubePosition malloc failed\n");
	}

	memset(mcCubePosition, 0, sizeSolution * 4 * sizeof(float));
	int tempp = 0;



	int* IsCube = NULL;
	IsCube = (int *)malloc(sizeSolution * sizeof(int));
	if (IsCube == NULL)
	{
		printf("IsCube malloc failed\n");
	}
	memset(IsCube, 0, sizeSolution * sizeof(int));
	printf("start update data\n");
	//std::cout << "start update data" << std::endl;
	//更新数据
	
	temp = tree.nextNode();
	num = 0;
	while (temp)
	{
		if (temp->indexOfcoefficient == -1) {
			temp = tree.nextNode(temp);
			continue;
		}
		Point3D<Real> center2 = Point3D<Real>{ 0,0,0 };
		Real width2 = 0;
		temp->centerAndWidth(center2, width2);
		width = width2;
		//位置坐标变为索引
		int x = (center2.coords[0] - xmin) / width;
		int y = (center2.coords[1] - ymin) / width;
		int z = (center2.coords[2] - zmin) / width;
		
		if (x < size_x  && y < size_y  && z < size_z)
		{
			if (abs(tranigles[x*size_y*size_z + y*size_z + z]) >0)
			{
				tempp++;
			}
			if (abs(tranigles[x*size_y*size_z + y*size_z + z + 1]) > 0)
			{
				tempp++;
			}
			if (abs(tranigles[x*size_y*size_z + (y + 1)*size_z + z])>0)
			{
				tempp++;
			}
			if (abs(tranigles[x*size_y*size_z + (y + 1)*size_z + z + 1])>0)
			{
				tempp++;
			}
			if (abs(tranigles[(x + 1)*size_y*size_z + y*size_z + z])>0)
			{
				tempp++;
			}
			if (abs(tranigles[(x + 1)*size_y*size_z + y*size_z + z + 1])>0)
			{
				tempp++;
			}
			if (abs(tranigles[(x + 1)*size_y*size_z + (y + 1)*size_z + z])>0)
			{
				tempp++;
			}
			if (abs(tranigles[(x + 1)*size_y*size_z + (y + 1)*size_z + z + 1]) >0)
			{
				tempp++;
			}
			//printf("%d\n", tempp);
			if (tempp == 8)
			{
				mcCubePosition[num * 4] = center2.coords[0];
				mcCubePosition[num * 4 + 1] = center2.coords[1];
				mcCubePosition[num * 4 + 2] = center2.coords[2];
				mcCubePosition[num * 4 + 3] = width;
				for (int off_z = 0; off_z < 2; off_z++)
				{
					for (int off_y = 0; off_y< 2; off_y++)
					{
						for (int off_x = 0; off_x < 2; off_x++)
						{
							//cout << (off_z << 2) + (off_y << 1) + (off_x) << endl;
							//cout << tranigles[(x + off_x)*size_y*size_z + (y + off_y)*size_z + (z + off_z)] << endl;
							mcCubeValue[num * 8 + (off_z << 2) + (off_y << 1) + (off_x)] = tranigles[(x + off_x)*size_y*size_z + (y + off_y)*size_z + (z + off_z)];
							//cout << indexOfCube * 8 + off_z << 2 + off_y << 1 + off_x << endl;
							fprintf(fp_result, "%f ", mcCubeValue[num * 8 + (off_z << 2) + (off_y << 1) + (off_x)]);
							//mcCubeValue[indexOfCube * 8 + (off_z << 2 + off_y << 1 + off_x)] = 1;
						}
					}
				}
				fprintf(fp_result, "\n");
				IsCube[num] = 1;

				indexOfCube++;
			}
		}
		num++;
		tempp = 0;
		temp = tree.nextNode(temp);
		
	}
	printf("indexOfCube %d\n", indexOfCube);
	//std::cout << "indexOfCube" << indexOfCube << std::endl;

	float *my_triangles = NULL;
	my_triangles = (float *)malloc(sizeSolution * 5 * 3 * 3 * sizeof(float));
	if (mcCubePosition == NULL)
	{
		printf("my_triangles malloc failed\n");
	}
	//else
	//{
	//	printf("my_triangles has malloced\n");
	//}
	memset(my_triangles, 0, sizeSolution * 5 * 3 * 3 * sizeof(float));

	double iso = 0;
	double valueSum = 0, weightsum = 0;
	//for (int i = 0; i < samples.size(); i++)
	//{
	//	TreeOctNode *node = samples[i].node;
	//	const ProjectiveData< OrientedPoint3D< Real >, Real >& sample = samples[i].sample;
	//	int nodeIndexInSpare = normalField.nodeIndexInNormal(node);
	//	Real w = sample.weight;
	//	if (w > 0)
	//	{
	//		valueSum += fx_right[nodeIndexInSpare * 7] * w;
	//		weightsum += w;
	//	}
	//	
	//}
	//iso = valueSum / weightsum;


	//求 iosvalue
	//FILE *fp_resultIn = NULL;
	//fp_resultIn = fopen("surfaceValue.txt", "w");
	//for (int i = 0; i < samples.size(); i++)
	//{
	//	TreeOctNode *node = samples[i].node;
	//	int nodeIndexInSpare = normalField.nodeIndexInNormal(node);
	//	fprintf(fp_resultIn, "%f\n", fx_right[nodeIndexInSpare * 7]);
	//}
	//fclose(fp_resultIn);
	for (int k = 0; k < fx_right.size(); k++)
	{
		if (k % 7 == 0)
		{
			iso += fx_right[k];
		}

	}
	iso = iso / (fx_right.size() / 7);
	//iso = -10;
	printf("iso %f\n", iso);
	//std::cout << "iso" << iso << std::endl;
	cudaError_t cudaStatus;

	cudaFree(0);
	cudaStatus = MC(mcCubeValue, IsCube, mcCubePosition, sizeSolution, my_triangles, iso);


	char *outfile = "output.ply";
	write_ply(my_triangles, sizeSolution * 5 * 3 * 3, outfile, IsCube);
	free(mcCubePosition);
	mcCubePosition = NULL;
	free(mcCubeValue);
	mcCubeValue = NULL;
	free(IsCube);
	IsCube = NULL;
	free(my_triangles);
	my_triangles = NULL;
	free(sizeOfDivergence);
	sizeOfDivergence = NULL;
	free(sizeOfcoefficient);
	sizeOfcoefficient = NULL;
}


template<int Degree>
void Octree<Degree>::ComputeDivergence(void)
{
	TreeOctNode* temp;
	fData.setDotTables(fData.DOT_FLAG | fData.D_DOT_FLAG);
	DivergenceFunction df;
	df.ot = this;
	temp = tree.nextNode();
	int num = 0;
	while (temp) {
		if (temp->nodeData.nodeIndex<0 || Length((*normals)[temp->nodeData.nodeIndex]) <= EPSILON) {
			temp = tree.nextNode(temp);
			continue;
		}
		int d = temp->depth();
		df.normal = (*normals)[temp->nodeData.nodeIndex];
		
		df.index[0] = int(temp->off[0]);
		df.index[1] = int(temp->off[1]);
		df.index[2] = int(temp->off[2]);
		//printf("%d %d %d\n", temp->off[0], temp->off[1],temp->off[2]);
		//printf("fData.depth %d tree.depth %d\n", fData.depth, tree.depth());
		TreeOctNode::ProcessNodeAdjacentNodesTocomputedivergence(fData.depth, temp, width, &tree, width, &df);
		num++;
		temp = tree.nextNode(temp);
	}
	printf("num of normal %d\n", num);
	fData.clearDotTables(fData.D_DOT_FLAG);

	MemoryUsage();
}

template<int Degree>
void Octree<Degree>::ComputeB(void)
{
	TreeOctNode* temp;
	fData.setDotTables(fData.DOT_FLAG | fData.D_DOT_FLAG);
	DivergenceFunction df;
	df.ot = this;

	temp = tree.nextNode();
	int num = 0;

	while (temp) {
		if (temp->divergence == 0) {
			//printf("temp->divergence %f\n", temp->divergence);
			temp = tree.nextNode(temp);
			//num++;
			continue;
		}
		int d = temp->depth();
		df.index[0] = int(temp->off[0]);
		df.index[1] = int(temp->off[1]);
		df.index[2] = int(temp->off[2]);
		TreeOctNode::ProcessNodeAdjacentNodesTocomputeB(fData.depth, temp, width, &tree, width, &df);
		//if (temp->indexOfdivergence != -1) { num++; }
		num++;
		temp = tree.nextNode(temp);
	}
	printf("num of divergence %d \n", num);
	num = 0;
	temp = tree.nextNode();
	while (temp)
	{
		if (temp->b == 0)
		{
			temp = tree.nextNode(temp);
			continue;
		}
		num++;
		temp = tree.nextNode(temp);
	}
	printf("num of b %d \n", num);
	fData.clearDotTables(fData.D_DOT_FLAG);

	MemoryUsage();
}
template<int Degree>
void Octree<Degree>::DivergenceFunction::Function(TreeOctNode* node1,const TreeOctNode* node2){
	Point3D<Real> n=normal;
	if(FunctionData<Degree,Real>::SymmetricIndex(index[0],int(node1->off[0]),scratch[0])){n.coords[0]=-n.coords[0];}
	if(FunctionData<Degree,Real>::SymmetricIndex(index[1],int(node1->off[1]),scratch[1])){n.coords[1]=-n.coords[1];}
	if(FunctionData<Degree,Real>::SymmetricIndex(index[2],int(node1->off[2]),scratch[2])){n.coords[2]=-n.coords[2];}
	double dot=ot->fData.dotTable[scratch[0]]*ot->fData.dotTable[scratch[1]]*ot->fData.dotTable[scratch[2]];
	node1->nodeData.value+=Real(dot*(ot->fData.dDotTable[scratch[0]]*n.coords[0]+ot->fData.dDotTable[scratch[1]]*n.coords[1]+ot->fData.dDotTable[scratch[2]]*n.coords[2]));
}
template<int Degree>
void Octree<Degree>::LaplacianProjectionFunction::Function(TreeOctNode* node1,const TreeOctNode* node2){
	scratch[0]=FunctionData<Degree,Real>::SymmetricIndex(index[0],int(node1->off[0]));
	scratch[1]=FunctionData<Degree,Real>::SymmetricIndex(index[1],int(node1->off[1]));
	scratch[2]=FunctionData<Degree,Real>::SymmetricIndex(index[2],int(node1->off[2]));
	node1->nodeData.value-=Real(ot->GetLaplacian(scratch)*value);
}


template<int Degree>
void Octree<Degree>::MyDivergenceFuntion::Function(TreeOctNode *node1, const TreeOctNode *node2, double *divergence_normal_Value, int *divergence_normal_SampleIndex, int * divergence_normal_EachLength, int *sizeOfDivergence, int column)
{
	if (node1->depth() != 5)
	{
		printf("MyDivergenceFunction %d\n", node1->depth());
	}

	int off1[3], off2[3];
	//off2[0] = index[0];
	//off2[1] = index[1];
	//off2[2] = index[2];
	double divergence = 0;
	double c1[3], w1[3], c2[3], w2[3];

	for (int i = 0; i < 3; i++)
	{
		off1[i] = node1->off[i];
		off2[i] = node2->off[i];
		BinaryNode<double>::CenterAndWidth(off1[i], c1[i], w1[i]);
		BinaryNode<double>::CenterAndWidth(off2[i], c2[i], w2[i]);
	}


	//计算散度
	PPolynomial<Degree> basefunction = ot->fData.baseFunction;
	PPolynomial<Degree> base1 = basefunction.scale(w2[0]).shift(c2[0]);
	//printf("%f %f %f\n", base(c2), base(c1),basefunction((c1-c2)/w2));
	PPolynomial<Degree - 1> dbasefunction1 = base1.derivative();

	//PPolynomial<Degree - 1> dbase = basefunction.derivative();



	PPolynomial<Degree> base2 = basefunction.scale(w2[1]).shift(c2[1]);
	//printf("%f %f %f\n", base(c2), base(c1),basefunction((c1-c2)/w2));
	PPolynomial<Degree - 1> dbasefunction2 = base2.derivative();




	PPolynomial<Degree> base3 = basefunction.scale(w2[2]).shift(c2[2]);
	//printf("%f %f %f\n", base(c2), base(c1),basefunction((c1-c2)/w2));
	PPolynomial<Degree - 1> dbasefunction3 = base3.derivative();

	//double Fx = dbasefunction1(c1[0])*base2(c1[1])*base3(c1[2]);
	//double Fy = base1(c1[0])*dbasefunction2(c1[1])*base3(c1[2]);
	//double Fz = base1(c1[0])*base2(c1[1])*dbasefunction3(c1[2]);
	double Fx = 0;
	double Fy = 0;
	double Fz = 0;//产生随机数
	int unitNumInResidue = 20;
	srand((int)time(0));
	Point3D<Real> &start = Point3D<Real>{0,0,0};
	start.coords[0] = c1[0] - w1[0] / 2;
	start.coords[1] = c1[1] - w1[1] / 2;
	start.coords[2] = c1[2] - w1[2] / 2;
	for (int m = 0; m < unitNumInResidue; m++)
	{
		double x_residue, y_residue, z_residue = 0;
		x_residue = (rand() / (double)RAND_MAX)*(w1[0])+start.coords[0];
		y_residue = (rand() / (double)RAND_MAX)*(w2[0])+start.coords[1];
		z_residue = (rand() / (double)RAND_MAX)*(w1[2])+start.coords[2];
		//cout << x_residue << " " << y_residue << " " << z_residue << " " << nodeIndexInSpare << " " << _nodeIndexInSpare << endl;

		
		Fx += dbasefunction1(x_residue)*base2(y_residue)*base3(z_residue);
		Fy += base1(x_residue)*dbasefunction2(y_residue)*base3(z_residue);
		Fz += base1(x_residue)*base2(y_residue)*dbasefunction3(z_residue);
	}
	Fx /= unitNumInResidue;
	Fy /= unitNumInResidue;
	Fz /= unitNumInResidue;

	//divergence = dbasefunction1(c1[0])*base2(c1[1])*base3(c1[2])*n.coords[0] + base1(c1[0])*dbasefunction2(c1[1])*base3(c1[2])*n.coords[1] + base1(c1[0])*base2(c1[1])*dbasefunction3(c1[2])*n.coords[2];

	//Fx Fy Fz为node function在x y z 方向上的导数 Fx=F1x'F1yF1z F=F1xF1yF1z
	if (Fx||Fy||Fz)
	{
		if (node1->indexOfdivergence == -1)
		{
			node1->indexOfdivergence = *sizeOfDivergence;
			++ *sizeOfDivergence;
			//printf("%d %d\n", node1->nodeData.mcIndex, node1->indexOfdivergence);
		}
		int indexOfdivergence = node1->indexOfdivergence;
		//int nodeIndex = node2->nodeData.nodeIndex;
		*(divergence_normal_Value + indexOfdivergence*column + *(divergence_normal_EachLength + indexOfdivergence)) = -Fx;
		*(divergence_normal_SampleIndex + indexOfdivergence*column + *(divergence_normal_EachLength + indexOfdivergence)) = nodeIndex * 3;
		++*(divergence_normal_EachLength + indexOfdivergence);
		*(divergence_normal_Value + indexOfdivergence*column + *(divergence_normal_EachLength + indexOfdivergence)) = -Fy;
		*(divergence_normal_SampleIndex + indexOfdivergence*column + *(divergence_normal_EachLength + indexOfdivergence)) = nodeIndex * 3 + 1;
		++*(divergence_normal_EachLength + indexOfdivergence);
		*(divergence_normal_Value + indexOfdivergence*column + *(divergence_normal_EachLength + indexOfdivergence)) = -Fz;
		*(divergence_normal_SampleIndex + indexOfdivergence*column + *(divergence_normal_EachLength + indexOfdivergence)) = nodeIndex * 3 + 2;
		++*(divergence_normal_EachLength + indexOfdivergence);
	}
	
	
}

bool cmp2(sortDiv a, sortDiv b)
{
	return a.index<b.index;//按照学号降序排列
						   //return a.id<b.id;//按照学号升序排列
}

template<int Degree>
void Octree<Degree>::GreenFunction::Function(TreeOctNode *node1, const TreeOctNode *node2, double *divergence_normal_Value, int *divergence_normal_SampleIndex, int * divergence_normal_EachLength, double *coefficient, int *coefficient_SampleIndex, int * coefficient_EachLength,int *sizeOfcoefficient, int column)
{
	if (node1->depth() != 5)
	{
		printf("GreenFunction %d\n", node1->depth());
	}

	//node2为node1的邻居 更新node1
	if (node1->indexOfcoefficient == -1)
	{
		node1->indexOfcoefficient = *sizeOfcoefficient;
		++ *sizeOfcoefficient;
	}
	Point3D<Real> &center1 = Point3D<Real>{ 0,0,0 };
	Real width1=0;
	node1->centerAndWidth(center1, width1);
//	printf(" centerAndWidth %f %f %f %f\n", center1.coords[0], center1.coords[1], center1.coords[2], width1);
	double f = 0, fxx = 0, fxy = 0, fxz = 0, fyy = 0, fyz = 0, fzz = 0;
	int nodeIndex1 = node1->nodeData.nodeIndex;
	int nodeIndex2 = node2->nodeData.nodeIndex;

	

	if (nodeIndex1 == nodeIndex2)
	{
		//	printf("11\n");
		Point3D<Real> &start = Point3D<Real>{0,0,0};

		start.coords[0] = center2[0] - width2 / 2;
		start.coords[1] = center2[1] - width2 / 2;
		start.coords[2] = center2[2] - width2 / 2;
		//cout << start[0] << " " << start[1] << " " << start[2] << endl;
		//将立方体中的格林函数分为球体和剩余部分 两部分来计算
		double radius = width2 / 4;

		double f_radius = 2 * PI*radius*radius;
		double f_residue = 0;
		double fxx_residue = 0, fxy_residue = 0, fxz_residue = 0, fyy_residue = 0, fyz_residue = 0, fzz_residue = 0;//Sd is second derivative
																													//产生随机数
		int unitNumInResidue = 200;
		srand((int)time(0));
		for (int m = 0; m < unitNumInResidue; m++)
		{
			int gaoya = 0;
			//cout << m << endl;
			double x_residue, y_residue, z_residue = 0;
			x_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[0];
			y_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[1];
			z_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[2];
			double dist = sqrt((x_residue - center2[0])*(x_residue - center2[0]) + (y_residue - center2[1])*(y_residue - center2[1]) + (z_residue - center2[2])*(z_residue - center2[2]));
			//保证随机产生的点在residue中
			while (dist < radius)
			{
				gaoya++;
				x_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[0];
				y_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[1];
				z_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[2];

				dist = sqrt((x_residue - center2[0])*(x_residue - center2[0]) + (y_residue - center2[1])*(y_residue - center2[1]) + (z_residue - center2[2])*(z_residue - center2[2]));
				//cout <<"dist"<< dist << endl;
			}
			//cout << x_residue << " " << y_residue << " "<<z_residue << " "<<dist<<" "<<radius<<" "<<nodeIndexInSpare<<" "<<_nodeIndexInSpare<<" "<<gaoya<<endl;
			//compute the differential of Green
			/*(3 * (2 * x - 2 * x1) ^ 2) / (4 * ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (5 / 2)) - 1 / ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (3 / 2)
			(3 * (2 * x - 2 * x1)*(2 * y - 2 * y1)) / (4 * ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (5 / 2))
			(3 * (2 * x - 2 * x1)*(2 * z - 2 * z1)) / (4 * ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (5 / 2))
			(3 * (2 * y - 2 * y1) ^ 2) / (4 * ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (5 / 2)) - 1 / ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (3 / 2)
			(3 * (2 * y - 2 * y1)*(2 * z - 2 * z1)) / (4 * ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (5 / 2))
			(3 * (2 * z - 2 * z1) ^ 2) / (4 * ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (5 / 2)) - 1 / ((x - x1) ^ 2 + (y - y1) ^ 2 + (z - z1) ^ 2) ^ (3 / 2)*/

			//x,y,z are from centerOfNode and x1,y1,z1 are from centerOf_Node
			double x = center1.coords[0], y = center1.coords[1], z = center1.coords[2], x1 = x_residue, y1 = y_residue, z1 = z_residue;
			double F = (x - x1)*(x - x1) + (y - y1)*(y - y1) + (z - z1)*(z - z1);
			fxx_residue += (3 * pow((2 * x - 2 * x1), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
			fxy_residue += (3 * (2 * x - 2 * x1)*(2 * y - 2 * y1)) / (4 * pow(F, 2.5));
			fxz_residue += (3 * (2 * x - 2 * x1)*(2 * z - 2 * z1)) / (4 * pow(F, 2.5));
			fyy_residue += (3 * pow((2 * y - 2 * y1), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
			fyz_residue += (3 * (2 * y - 2 * y1)*(2 * z - 2 * z1)) / (4 * pow(F, 2.5));
			fzz_residue += (3 * pow((2 * z - 2 * z1), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
			//double diffOfGreen = fxx*fxx + fxy*fxy + fxz*fxz + fyy*fyy + fyz*fyz + fzz*fzz;

			//printf("x %f y %f z %f x1 %f y1 %f z1 %f\n", x, y, z, x1, y1, z1);
			//printf("F %f fxx %f fxy %f fxz %f fyy %f fyz %f fzz  %f\n", F,fxx,fxy,fxz,fyy,fyz,fzz);
			f_residue += 1 / (4 * PI*sqrt(F));
			//cout << m <<" "<<gaoya<< endl;
		}
		f_residue /= unitNumInResidue;
		fxx_residue /= unitNumInResidue;
		fxy_residue /= unitNumInResidue;
		fxz_residue /= unitNumInResidue;
		fyy_residue /= unitNumInResidue;
		fyz_residue /= unitNumInResidue;
		fzz_residue /= unitNumInResidue;
		f = (f_radius + (width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*f_residue) / (width2*width2*width2);
		fxx = ((width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*fxx_residue) / (width2*width2*width2);
		fxy = ((width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*fxy_residue) / (width2*width2*width2);
		fxz = ((width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*fxz_residue) / (width2*width2*width2);
		fyy = ((width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*fyy_residue) / (width2*width2*width2);
		fyz = ((width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*fyz_residue) / (width2*width2*width2);
		fzz = ((width2*width2*width2 - (4 / 3)*PI*radius*radius*radius)*fzz_residue) / (width2*width2*width2);
	}
	else
	{
		//printf("11\n");
		Point3D<Real> &start = Point3D<Real>{0,0,0};
		start.coords[0] = center2[0] - width2 / 2;
		start.coords[1] = center2[1] - width2 / 2;
		start.coords[2] = center2[2] - width2 / 2;
		//cout << "2" << endl;
		//cout << start[0] << " " << start[1] << " " << start[2] << endl;
		double f_residue = 0;
		double fxx_residue = 0, fxy_residue = 0, fxz_residue = 0, fyy_residue = 0, fyz_residue = 0, fzz_residue = 0;//Sd is second derivative

		int unitNumInResidue = 200;
		srand((int)time(0));
		for (int m = 0; m < unitNumInResidue; m++)
		{
			double x_residue, y_residue, z_residue = 0;
			x_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[0];
			y_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[1];
			z_residue = (rand() / (double)RAND_MAX)*(width2)+start.coords[2];
			//cout << x_residue << " " << y_residue << " " << z_residue << " " << nodeIndexInSpare << " " << _nodeIndexInSpare << endl;

			double x = center1.coords[0], y = center1.coords[1], z = center1.coords[2], x1 = x_residue, y1 = y_residue, z1 = z_residue;
			double F = (x - x1)*(x - x1) + (y - y1)*(y - y1) + (z - z1)*(z - z1);
			fxx_residue += (3 * pow((2 * x - 2 * x1), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
			fxy_residue += (3 * (2 * x - 2 * x1)*(2 * y - 2 * y1)) / (4 * pow(F, 2.5));
			fxz_residue += (3 * (2 * x - 2 * x1)*(2 * z - 2 * z1)) / (4 * pow(F, 2.5));
			fyy_residue += (3 * pow((2 * y - 2 * y1), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
			fyz_residue += (3 * (2 * y - 2 * y1)*(2 * z - 2 * z1)) / (4 * pow(F, 2.5));
			fzz_residue += (3 * pow((2 * z - 2 * z1), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
			//double diffOfGreen = fxx*fxx + fxy*fxy + fxz*fxz + fyy*fyy + fyz*fyz + fzz*fzz;

			//printf("x %f y %f z %f x1 %f y1 %f z1 %f\n", x, y, z, x1, y1, z1);
			//printf("F %f fxx %f fxy %f fxz %f fyy %f fyz %f fzz  %f\n", F,fxx,fxy,fxz,fyy,fyz,fzz);
			f_residue += 1 / (4 * PI*sqrt(F));
		}
		f_residue /= unitNumInResidue;
		fxx_residue /= unitNumInResidue;
		fxy_residue /= unitNumInResidue;
		fxz_residue /= unitNumInResidue;
		fyy_residue /= unitNumInResidue;
		fyz_residue /= unitNumInResidue;
		fzz_residue /= unitNumInResidue;
		f = f_residue;
		fxx = fxx_residue;
		fxy = fxy_residue;
		fxz = fxz_residue;
		fyy = fyy_residue;
		fyz = fyz_residue;
		fzz = fzz_residue;
		//cout << "2" << endl;
	}

	int lamda = 10000;
	double LSValue[7] = { f,fxx / lamda,fxy / lamda,fxz / lamda,fyy / lamda,fyz / lamda,fzz / lamda };
	double F = (center1.coords[0] - center2[0])*(center1.coords[0] - center2[0]) + (center1.coords[1] - center2[1])*(center1.coords[1] - center2[1]) + (center1.coords[2] - center2[2])*(center1.coords[2] - center2[2]);
	double Fxx = (3 * pow((2 * center1.coords[0] - 2 * center2[0]), 2)) / (4 * pow(F, 2.5)) - 1 / (pow(F, 1.5));
	int _nodeIndexInSpare = node2->indexOfdivergence;
	int nodeIndexInSpare = node1->indexOfcoefficient;
	//printf("center1 %f %f %f center2 %f %f %f nodeindex1 %d nodeindex2 %d\n", center1.coords[0], center1.coords[1], center1.coords[2], center2[0], center2[1], center2[2], node1->nodeData.nodeIndex, node2->nodeData.nodeIndex);
//	printf("%f %f %f %f %f %f %f %f %f %d %d %d\n", 1 / (4 * PI*sqrt(F)), f, Fxx / lamda, fxx / lamda, fxy / lamda, fxz / lamda, fyy / lamda, fyz / lamda, fzz / lamda, nodeIndexInSpare, node1->indexOfdivergence,_nodeIndexInSpare);



	for (int indexLS = 0; indexLS < 7; indexLS++)
	{
		//div_green为散度乘以格林函数 相加即为f(x) 
		double *div_green_Value = NULL;
		//cout << "size div_green" << *(diverence_normal_EachLength + nodeIndexInSpare) << endl;
		div_green_Value = (double *)malloc(*(divergence_normal_EachLength + _nodeIndexInSpare) * sizeof(double));
		if (NULL == div_green_Value)
		{
			printf("div_green malloc failed\n");
		}
		memset(div_green_Value, 0, *(divergence_normal_EachLength + _nodeIndexInSpare) * sizeof(double));
		int *div_green_SampleIndex = NULL;
		div_green_SampleIndex = (int *)malloc(*(divergence_normal_EachLength + _nodeIndexInSpare) * sizeof(int));
		if (NULL == div_green_SampleIndex)
		{
			printf("div_green_SampleIndex malloc failed\n");
		}
		memset(div_green_SampleIndex, 0, *(divergence_normal_EachLength + _nodeIndexInSpare) * sizeof(int));
		int div_green_Length = 0;
		//div_green_Length = (int *)malloc(7 * sizeof(int));
		//memset(div_green_Length, 0, 7 * sizeof(int));
		//cout << 1 / sqrt(Green) << " " << diffOfGreen  << endl;
		//for (int m = 0; m < *(diverence_normal_EachLength + _nodeIndexInSpare); m++)

		sortDiv SD[900];
		for (int i = 0; i < *(divergence_normal_EachLength + _nodeIndexInSpare);i++)
		{ 
			SD[i].value = *(divergence_normal_Value + _nodeIndexInSpare*column + i);
			SD[i].index = *(divergence_normal_SampleIndex + _nodeIndexInSpare*column + i);
		}
		//printf("pre\n");
		//for (int i = 0; i < 20; i++)
		//{
		//	printf("%d ",  SD[i].index);
		//}
		//printf("\n");
		sort(SD, SD + *(divergence_normal_EachLength + _nodeIndexInSpare), cmp2);
		//printf("pro\n");
		//for (int i = 0; i < 20; i++)
		//{
		//	printf("%d ",  SD[i].index);
		//}
		//printf("\n");

		while (div_green_Length<*(divergence_normal_EachLength + _nodeIndexInSpare))
		{
			//cout << *(diverence_normal_Value + _nodeIndexInSpare*size_col*3 + div_green_Length) << endl;
			//*(div_green_Value + div_green_Length) = *(divergence_normal_Value + _nodeIndexInSpare*column + div_green_Length)*(LSValue[indexLS])*width2*width2*width2;
			//printf("%d %f\n", indexLS,(LSValue[indexLS])*width2*width2*width2);
			*(div_green_Value + div_green_Length) = SD[div_green_Length].value*(LSValue[indexLS])*width2*width2*width2;
			*(div_green_SampleIndex + div_green_Length) = SD[div_green_Length].index;
			
			
			//cout << *(diverence_normal_Value + nodeIndexInSpare*size_col*3 + div_green_Length) <<" "<<(1 / sqrt(Green) + diffOfGreen / 1000000)<< " "<< *(div_green_Value + div_green_Length) <<endl;
			//*(div_green_SampleIndex + div_green_Length) = *(divergence_normal_SampleIndex + _nodeIndexInSpare*column + div_green_Length);
			++div_green_Length;
		}
		if (div_green_Length != *(divergence_normal_EachLength + _nodeIndexInSpare))
		{
			printf("div_green_Length!=*(diverence_normal_EachLength+_nodeIndexInSpare)\n") ;
		}
		//cout << "1" << endl;
		//cout << *(coefficient_EachLength + nodeIndexInSpare) << endl;
		//cout << "shadow" << *(coefficient_EachLength + i) << " "<<div_green_Length<<" " << *(coefficient_EachLength + i) + div_green_Length << endl;
		double *shadowCoeValue = NULL;
		shadowCoeValue = (double *)malloc((*(coefficient_EachLength + nodeIndexInSpare * 7 + indexLS) + div_green_Length) * sizeof(double));
		if (shadowCoeValue == NULL)
		{
			printf("shadowCoeValue malloc failed\n");
		}
		memset(shadowCoeValue, 0, (*(coefficient_EachLength + nodeIndexInSpare * 7 + indexLS) + div_green_Length) * sizeof(double));

		int *shadowCoeSampleIndex = NULL;
		shadowCoeSampleIndex = (int *)malloc((*(coefficient_EachLength + nodeIndexInSpare * 7 + indexLS) + div_green_Length) * sizeof(int));
		if (shadowCoeSampleIndex == NULL)
		{
			printf("shadowCoeSampleIndex malloc failed\n");
		}
		memset(shadowCoeSampleIndex, 0, (*(coefficient_EachLength + nodeIndexInSpare * 7 + indexLS) + div_green_Length) * sizeof(int));

		int shadowCoeLength = 0;


		int indexCoe = 0;
		int	indexDivGreen = 0;
		while (indexCoe < *(coefficient_EachLength + nodeIndexInSpare * 7 + indexLS) && indexDivGreen < div_green_Length)
		{
			if (*(coefficient_SampleIndex + (nodeIndexInSpare * 7 + indexLS)*column + indexCoe) == *(div_green_SampleIndex + indexDivGreen))
			{
				*(shadowCoeValue + shadowCoeLength) = *(coefficient + (nodeIndexInSpare * 7 + indexLS)*column  + indexCoe) + *(div_green_Value + indexDivGreen);
				*(shadowCoeSampleIndex + shadowCoeLength) = *(coefficient_SampleIndex + (nodeIndexInSpare * 7 + indexLS)*column + indexCoe);
				shadowCoeLength++;
				indexCoe++;
				indexDivGreen++;
			}
			else if (*(coefficient_SampleIndex + (nodeIndexInSpare * 7 + indexLS)*column + indexCoe) < *(div_green_SampleIndex + indexDivGreen))
			{
				*(shadowCoeValue + shadowCoeLength) = *(coefficient + (nodeIndexInSpare * 7 + indexLS)*column + indexCoe);
				*(shadowCoeSampleIndex + shadowCoeLength) = *(coefficient_SampleIndex + (nodeIndexInSpare * 7 + indexLS)*column+ indexCoe);
				shadowCoeLength++;
				indexCoe++;
			}
			else
			{
				*(shadowCoeValue + shadowCoeLength) = *(div_green_Value + indexDivGreen);
				*(shadowCoeSampleIndex + shadowCoeLength) = *(div_green_SampleIndex + indexDivGreen);
				shadowCoeLength++;
				indexDivGreen++;
			}
		}
		//cout << "endwhile" << endl;
		if (indexCoe < *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS)))
		{
			for (int m = indexCoe; m < *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS)); m++)
			{
				*(shadowCoeValue + shadowCoeLength) = *(coefficient + (nodeIndexInSpare * 7 + indexLS)*column + m);
				*(shadowCoeSampleIndex + shadowCoeLength) = *(coefficient_SampleIndex + (nodeIndexInSpare * 7 + indexLS)*column+ m);
				shadowCoeLength++;
			}
		}
		if (indexDivGreen < div_green_Length)
		{
			for (int m = indexDivGreen; m < div_green_Length; m++)
			{
				*(shadowCoeValue + shadowCoeLength) = *(div_green_Value + m);
				*(shadowCoeSampleIndex + shadowCoeLength) = *(div_green_SampleIndex + m);
				shadowCoeLength++;
			}
		}
		*(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS)) = 0;
		//cout << "shadowCoeLength" << " " << shadowCoeLength << endl;
		for (int m = 0; m < shadowCoeLength; m++)
		{
			*(coefficient + (nodeIndexInSpare * 7 + indexLS)*column + *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS))) = *(shadowCoeValue + *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS)));
			*(coefficient_SampleIndex + (nodeIndexInSpare * 7 + indexLS)*column + *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS))) = *(shadowCoeSampleIndex + *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS)));
			++ *(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS));
		}
		if (*(coefficient_EachLength + (nodeIndexInSpare * 7 + indexLS)) != shadowCoeLength)
		{
			printf("coefficient_EachLength error and i is %d\n", (nodeIndexInSpare * 7 + indexLS));
		}


		free(shadowCoeSampleIndex);
		shadowCoeSampleIndex = NULL;

		free(shadowCoeValue);
		shadowCoeValue = NULL;

		free(div_green_SampleIndex);
		div_green_SampleIndex = NULL;

		free(div_green_Value);
		div_green_Value = NULL;
		//cout << "22" << endl;
	}//indexLS
}//end



template<int Degree>
void Octree<Degree>::DivergenceFunction::computedivergence(TreeOctNode* node1, const TreeOctNode* node2) {
	//更新node1 node2为基函数
	int off1[3], off2[3];
	double divergence = 0;
	double c1[3], w1[3], c2[3], w2[3];
	Point3D<Real> n = normal;
	for (int i = 0; i < 3; i++)
	{
		off1[i] = node1->off[i];
		off2[i] = node2->off[i];
		BinaryNode<double>::CenterAndWidth(off1[i], c1[i], w1[i]);
		BinaryNode<double>::CenterAndWidth(off2[i], c2[i], w2[i]);
	}

	//if (FunctionData<Degree, Real>::SymmetricIndex(index[0], int(node1->off[0]), scratch[0])) { n.coords[0] = -n.coords[0]; }
	//if (FunctionData<Degree, Real>::SymmetricIndex(index[1], int(node1->off[1]), scratch[1])) { n.coords[1] = -n.coords[1]; }
	//if (FunctionData<Degree, Real>::SymmetricIndex(index[2], int(node1->off[2]), scratch[2])) { n.coords[2] = -n.coords[2]; }

	//计算散度
	PPolynomial<Degree> basefunction = ot->fData.baseFunction;
	PPolynomial<Degree> base1 = basefunction.scale(w2[0]).shift(c2[0]);
	//printf("%f %f %f\n", base(c2), base(c1),basefunction((c1-c2)/w2));
	PPolynomial<Degree - 1> dbasefunction1 = base1.derivative();

	PPolynomial<Degree - 1> dbase = basefunction.derivative();
	//basefunction.printnl();

	
	PPolynomial<Degree> base2 = basefunction.scale(w2[1]).shift(c2[1]);
	//printf("%f %f %f\n", base(c2), base(c1),basefunction((c1-c2)/w2));
	PPolynomial<Degree - 1> dbasefunction2 = base2.derivative();



	
	PPolynomial<Degree> base3 = basefunction.scale(w2[2]).shift(c2[2]);
	//printf("%f %f %f\n", base(c2), base(c1),basefunction((c1-c2)/w2));
	PPolynomial<Degree - 1> dbasefunction3 = base3.derivative();
	////printf("off %d %d %d %d %d %d\n", off2[0], off2[1], off2[2], index[0], index[1], index[2]);
	//printf("off %d %d %d %d %d %d\n", off2[0], off2[1], off2[2], off1[0], off1[1], off1[2]);
	//printf("%f %f %f\n", n.coords[0], n.coords[1], n.coords[2]);
	divergence = dbasefunction1(c1[0])*base2(c1[1])*base3(c1[2])*n.coords[0] + base1(c1[0])*dbasefunction2(c1[1])*base3(c1[2])*n.coords[1] + base1(c1[0])*base2(c1[1])*dbasefunction3(c1[2])*n.coords[2];
	//printf("%f\n", divergence);
	//printf("divergence %f dbase %f %f %f dbase %f %f %f w %f \n",divergence, dbasefunction1(c1[0]), dbasefunction2(c1[1]), dbasefunction3(c1[2]),  dbase((c1[0]-c2[0])/w2[0]), dbase((c1[1] - c2[1]) / w2[1]), dbase((c1[2] - c2[2]) / w2[2]),w2[0]);
	//int d = node2->d;
	//Real width = 0;
	//Point3D<Real> center;
	//node2->centerAndWidth(center, width);
	//printf("%d %d %f %f\n",node2->depth(), d,width,w2[0]);
	node1->divergence += -divergence;
	//printf("%f %f %f %f %f %f %f %f %f %f %f %f\n", dbasefunction1(c1[0]), base2(c1[1]), base3(c1[2]), base1(c1[0]), dbasefunction2(c1[1]), base3(c1[2]), base1(c1[0]), base2(c1[1]), dbasefunction3(c1[2]), (c1[0] - c2[0]) / w2[0], (c1[1] - c2[1]) / w2[1], (c1[2] - c2[2]) / w2[2]);
	//printf("%d %d %d %d\n", node1->depth(), node1->d, node2->depth(), node2->d);
	//printf("%f %f\n", node1->divergence,divergence);
}

template<int Degree>
void Octree<Degree>::DivergenceFunction::computeB(TreeOctNode*node1, const TreeOctNode *node2)
{
	//off1为基函数 更新off1
	int off1[3], off2[3];
	double nodevalue = 1;
	for (int i = 0; i < 3; i++)
	{
		off1[i] = node1->off[i];
		off2[i] = node2->off[i];
		double  c1, w1, c2, w2;
		//PPolynomial<Degree> basefunction = fData.dbaseFunctions[off1[i]];
		PPolynomial<Degree> basefunction = ot->fData.baseFunction;
		BinaryNode<double>::CenterAndWidth(off1[i], c1, w1);
		PPolynomial<Degree> base = basefunction.scale(w1).shift(c1);
		BinaryNode<double>::CenterAndWidth(off2[i], c2, w2);
		//printf("base %f %f %f %f \n", base(c2), w1,(c2-c1)/w1,basefunction((c2 - c1) / w1));
		nodevalue *= base(c2)*w2;
	}
	//printf("diverence %f node value %f\n",node2->divergence, nodevalue);
	node1->b += nodevalue*node2->divergence;
	//printf("%f \n", nodevalue*node2->divergence);
}



template<int Degree>
void Octree<Degree>::AdjacencyCountFunction::Function(const TreeOctNode* node1,const TreeOctNode* node2){adjacencyCount++;}
template<int Degree>
void Octree<Degree>::AdjacencySetFunction::Function(const TreeOctNode* node1,const TreeOctNode* node2){adjacencies[adjacencyCount++]=node1->nodeData.nodeIndex;}
template<int Degree>
void Octree<Degree>::RefineFunction::Function(TreeOctNode* node1,const TreeOctNode* node2){
	if(!node1->children && node1->depth()<depth){node1->initChildren();}
}
template<int Degree>
void Octree<Degree>::FaceEdgesFunction::Function(const TreeOctNode* node1,const TreeOctNode* node2){
	if(!node1->children && MarchingCubes::HasRoots(node1->nodeData.mcIndex)){
		RootInfo ri1,ri2;
		hash_map<long long,std::pair<RootInfo,int> >::iterator iter;
		int isoTri[DIMENSION*MarchingCubes::MAX_TRIANGLES];
		int count=MarchingCubes::AddTriangleIndices(node1->nodeData.mcIndex,isoTri);

		for(int j=0;j<count;j++){
			for(int k=0;k<3;k++){
				if(fIndex==Cube::FaceAdjacentToEdges(isoTri[j*3+k],isoTri[j*3+((k+1)%3)])){
					if(GetRootIndex(node1,isoTri[j*3+k],maxDepth,ri1) && GetRootIndex(node1,isoTri[j*3+((k+1)%3)],maxDepth,ri2)){
						edges->push_back(std::pair<long long,long long>(ri2.key,ri1.key));
						iter=vertexCount->find(ri1.key);
						if(iter==vertexCount->end()){
							(*vertexCount)[ri1.key].first=ri1;
							(*vertexCount)[ri1.key].second=0;
						}
						iter=vertexCount->find(ri2.key);
						if(iter==vertexCount->end()){
							(*vertexCount)[ri2.key].first=ri2;
							(*vertexCount)[ri2.key].second=0;
						}
						(*vertexCount)[ri1.key].second--;
						(*vertexCount)[ri2.key].second++;
					}
					else{fprintf(stderr,"Bad Edge 1: %d %d\n",ri1.key,ri2.key);}
				}
			}
		}
	}
}
template<int Degree>
void Octree<Degree>::PointIndexValueFunction::Function(const TreeOctNode* node){
	int idx[DIMENSION];
	idx[0]=index[0]+int(node->off[0]);
	idx[1]=index[1]+int(node->off[1]);
	idx[2]=index[2]+int(node->off[2]);
	value+=node->nodeData.value*   Real( valueTables[idx[0]]* valueTables[idx[1]]* valueTables[idx[2]]);
}
template<int Degree>
void Octree<Degree>::PointIndexValueAndNormalFunction::Function(const TreeOctNode* node){
	int idx[DIMENSION];
	idx[0]=index[0]+int(node->off[0]);
	idx[1]=index[1]+int(node->off[1]);
	idx[2]=index[2]+int(node->off[2]);
	value+=				node->nodeData.value*   Real( valueTables[idx[0]]* valueTables[idx[1]]* valueTables[idx[2]]);
	normal.coords[0]+=	node->nodeData.value*   Real(dValueTables[idx[0]]* valueTables[idx[1]]* valueTables[idx[2]]);
	normal.coords[1]+=	node->nodeData.value*   Real( valueTables[idx[0]]*dValueTables[idx[1]]* valueTables[idx[2]]);
	normal.coords[2]+=	node->nodeData.value*   Real( valueTables[idx[0]]* valueTables[idx[1]]*dValueTables[idx[2]]);
}
template<int Degree>
int Octree<Degree>::LaplacianMatrixFunction::Function(const TreeOctNode* node1,const TreeOctNode* node2){
	Real temp;
	int d1=int(node1->d);
	int x1,y1,z1;
	x1=int(node1->off[0]);
	y1=int(node1->off[1]);
	z1=int(node1->off[2]);
	int dDepth=d2-d1;
	int d;
	d=(x2>>dDepth)-x1;
	if(d<0){return 0;}
	if(!dDepth){
		if(!d){
			d=y2-y1;
			if(d<0){return 0;}
			else if(!d){
				d=z2-z1;
				if(d<0){return 0;}
			}
		}
		scratch[0]=FunctionData<Degree,Real>::SymmetricIndex(index[0],x1);
		scratch[1]=FunctionData<Degree,Real>::SymmetricIndex(index[1],y1);
		scratch[2]=FunctionData<Degree,Real>::SymmetricIndex(index[2],z1);
		temp=ot->GetLaplacian(scratch);
		if(node1==node2){temp/=2;}
		if(fabs(temp)>EPSILON){
			rowElements[elementCount].Value=temp;
			rowElements[elementCount].N=node1->nodeData.nodeIndex-offset;
			elementCount++;
		}
		return 0;
	}
	return 1;
}

template<int Degree>
int Octree<Degree>::RestrictedLaplacianMatrixFunction::Function(const TreeOctNode* node1,const TreeOctNode* node2){
	int d1,d2,off1[3],off2[3];
	node1->depthAndOffset(d1,off1);
	node2->depthAndOffset(d2,off2);
	int dDepth=d2-d1;
	int d;
	d=(off2[0]>>dDepth)-off1[0];
	if(d<0){return 0;}

	if(!dDepth){
		if(!d){
			d=off2[1]-off1[1];
			if(d<0){return 0;}
			else if(!d){
				d=off2[2]-off1[2];
				if(d<0){return 0;}
			}
		}
		// Since we are getting the restricted matrix, we don't want to propogate out to terms that don't contribute...
		if(!TreeOctNode::Overlap2(depth,offset,0.5,d1,off1,radius)){return 0;}
		scratch[0]=FunctionData<Degree,Real>::SymmetricIndex(index[0],BinaryNode<Real>::Index(d1,off1[0]));
		scratch[1]=FunctionData<Degree,Real>::SymmetricIndex(index[1],BinaryNode<Real>::Index(d1,off1[1]));
		scratch[2]=FunctionData<Degree,Real>::SymmetricIndex(index[2],BinaryNode<Real>::Index(d1,off1[2]));
		Real temp=ot->GetLaplacian(scratch);
		if(node1==node2){temp/=2;}
		if(fabs(temp)>EPSILON){
			rowElements[elementCount].Value=temp;
			rowElements[elementCount].N=node1->nodeData.nodeIndex;
			elementCount++;
		}
		return 0;
	}
	return 1;
}

template<int Degree>
void Octree<Degree>::GetMCIsoTriangles(const Real& isoValue,CoredMeshData* mesh,const int& fullDepthIso,const int& nonLinearFit , bool addBarycenter , bool polygonMesh ){
	double t;
	TreeOctNode* temp;

	hash_map<long long,int> roots;
	hash_map<long long,std::pair<Real,Point3D<Real> > > *normalHash=new hash_map<long long,std::pair<Real,Point3D<Real> > >();

	SetIsoSurfaceCorners(isoValue,0,fullDepthIso);
	// At the point all of the corner values have been set and all nodes are valid. Now it's just a matter
	// of running marching cubes.

	t=Time();
	fData.setValueTables(fData.VALUE_FLAG | fData.D_VALUE_FLAG,0,postNormalSmooth);
	temp=tree.nextLeaf();
	while(temp){
		SetMCRootPositions(temp,0,isoValue,roots,NULL,*normalHash,NULL,NULL,mesh,nonLinearFit);
		temp=tree.nextLeaf(temp);
	}
	MemoryUsage();

	DumpOutput("Normal Size: %.2f MB\n",double(sizeof(Point3D<Real>)*normalHash->size())/1000000);
	DumpOutput("Set %d root positions in: %f\n",mesh->inCorePoints.size(),Time()-t);
	DumpOutput("Memory Usage: %.3f MB\n",float(MemoryUsage()));

	fData.clearValueTables();
	delete normalHash;

	DumpOutput("Post deletion size: %.3f MB\n",float(MemoryUsage()));

	t=Time();

	// Now get the iso-surfaces, running from finest nodes to coarsest in order to allow for edge propogation from
	// finer faces to coarser ones.
	temp=tree.nextLeaf();
	while(temp)
	{
		GetMCIsoTriangles(temp,mesh,roots,NULL,NULL,0,0 , addBarycenter , polygonMesh );
		temp=tree.nextLeaf(temp);
	}
	DumpOutput("Added triangles in: %f\n",Time()-t);
	DumpOutput("Memory Usage: %.3f MB\n",float(MemoryUsage()));
}
template<int Degree>
void Octree<Degree>::GetMCIsoTriangles(const Real& isoValue,const int& subdivideDepth,CoredMeshData* mesh,const int& fullDepthIso,const int& nonLinearFit , bool addBarycenter , bool polygonMesh )
{
	TreeOctNode* temp;
	hash_map<long long,int> boundaryRoots,*interiorRoots;
	hash_map<long long,std::pair<Real,Point3D<Real> > > *boundaryNormalHash,*interiorNormalHash;
	std::vector<Point3D<float> >* interiorPoints;

	int sDepth;
	if(subdivideDepth<=0){sDepth=0;}
	else{sDepth=fData.depth-subdivideDepth;}
	if(sDepth<0){sDepth=0;}

	SetIsoSurfaceCorners(isoValue,sDepth,fullDepthIso);
	// At this point all of the corner values have been set and all nodes are valid. Now it's just a matter
	// of running marching cubes.

	boundaryNormalHash=new hash_map<long long,std::pair<Real,Point3D<Real> > >();
	int offSet=0;
	SortedTreeNodes sNodes;
	sNodes.set(tree,0);
	fData.setValueTables(fData.VALUE_FLAG | fData.D_VALUE_FLAG,0,postNormalSmooth);

	// Set the root positions for all leaf nodes below the subdivide threshold
	SetBoundaryMCRootPositions(sDepth,isoValue,boundaryRoots,*boundaryNormalHash,mesh,nonLinearFit);

	for(int i=sNodes.nodeCount[sDepth];i<sNodes.nodeCount[sDepth+1];i++){
		interiorRoots=new hash_map<long long,int>();
		interiorNormalHash=new hash_map<long long,std::pair<Real,Point3D<Real> > >();
		interiorPoints=new std::vector<Point3D<float> >();

		temp=sNodes.treeNodes[i]->nextLeaf();
		while(temp){
			if(MarchingCubes::HasRoots(temp->nodeData.mcIndex)){
				SetMCRootPositions(temp,sDepth,isoValue,boundaryRoots,interiorRoots,*boundaryNormalHash,interiorNormalHash,interiorPoints,mesh,nonLinearFit);
			}
			temp=sNodes.treeNodes[i]->nextLeaf(temp);
		}
		delete interiorNormalHash;

		temp=sNodes.treeNodes[i]->nextLeaf();
		while(temp){
			GetMCIsoTriangles(temp,mesh,boundaryRoots,interiorRoots,interiorPoints,offSet,sDepth , addBarycenter , polygonMesh );
			temp=sNodes.treeNodes[i]->nextLeaf(temp);
		}
		delete interiorRoots;
		delete interiorPoints;
		offSet=mesh->outOfCorePointCount();
	}
	delete boundaryNormalHash;

	temp=tree.nextLeaf();
	while(temp){
		if(temp->depth()<sDepth){GetMCIsoTriangles(temp,mesh,boundaryRoots,NULL,NULL,0,0 , addBarycenter , polygonMesh );}
		temp=tree.nextLeaf(temp);
	}
}
template<int Degree>
Real Octree<Degree>::getCenterValue(const TreeOctNode* node){
	int idx[3];
	Real value=0;

	neighborKey2.getNeighbors(node);
	VertexData::CenterIndex(node,fData.depth,idx);
	idx[0]*=fData.res;
	idx[1]*=fData.res;
	idx[2]*=fData.res;
	for(int i=0;i<=node->depth();i++){
		for(int j=0;j<3;j++){
			for(int k=0;k<3;k++){
				for(int l=0;l<3;l++){
					const TreeOctNode* n=neighborKey2.neighbors[i].neighbors[j][k][l];
					if(n){
						Real temp=n->nodeData.value;
						value+=temp*Real(
							fData.valueTables[idx[0]+int(n->off[0])]*
							fData.valueTables[idx[1]+int(n->off[1])]*
							fData.valueTables[idx[2]+int(n->off[2])]);
					}
				}
			}
		}
	}
	if(node->children){
		for(int i=0;i<Cube::CORNERS;i++){
			int ii=Cube::AntipodalCornerIndex(i);
			const TreeOctNode* n=&node->children[i];
			while(1){
				value+=n->nodeData.value*Real(
					fData.valueTables[idx[0]+int(n->off[0])]*
					fData.valueTables[idx[1]+int(n->off[1])]*
					fData.valueTables[idx[2]+int(n->off[2])]);
				if(n->children){n=&n->children[ii];}
				else{break;}
			}
		}
	}
	return value;
}
template<int Degree>
Real Octree<Degree>::getCornerValue(const TreeOctNode* node,const int& corner){
	int idx[3];
	Real value=0;

	neighborKey2.getNeighbors(node);
	VertexData::CornerIndex(node,corner,fData.depth,idx);
	idx[0]*=fData.res;
	idx[1]*=fData.res;
	idx[2]*=fData.res;
	for(int i=0;i<=node->depth();i++){
		for(int j=0;j<3;j++){
			for(int k=0;k<3;k++){
				for(int l=0;l<3;l++){
					const TreeOctNode* n=neighborKey2.neighbors[i].neighbors[j][k][l];
					if(n){
						Real temp=n->nodeData.value;
						value+=temp*Real(
							fData.valueTables[idx[0]+int(n->off[0])]*
							fData.valueTables[idx[1]+int(n->off[1])]*
							fData.valueTables[idx[2]+int(n->off[2])]);
					}
				}
			}
		}
	}
	int x,y,z,d=node->depth();
	Cube::FactorCornerIndex(corner,x,y,z);
	for(int i=0;i<2;i++){
		for(int j=0;j<2;j++){
			for(int k=0;k<2;k++){
				const TreeOctNode* n=neighborKey2.neighbors[d].neighbors[x+i][y+j][z+k];
				if(n){
					int ii=Cube::AntipodalCornerIndex(Cube::CornerIndex(i,j,k));
					while(n->children){
						n=&n->children[ii];
						value+=n->nodeData.value*Real(
							fData.valueTables[idx[0]+int(n->off[0])]*
							fData.valueTables[idx[1]+int(n->off[1])]*
							fData.valueTables[idx[2]+int(n->off[2])]);
					}
				}
			}
		}
	}
	return value;
}
template<int Degree>
void Octree<Degree>::getCornerValueAndNormal(const TreeOctNode* node,const int& corner,Real& value,Point3D<Real>& normal){
	int idx[3],index[3];
	value=normal.coords[0]=normal.coords[1]=normal.coords[2]=0;

	neighborKey2.getNeighbors(node);
	VertexData::CornerIndex(node,corner,fData.depth,idx);
	idx[0]*=fData.res;
	idx[1]*=fData.res;
	idx[2]*=fData.res;
	for(int i=0;i<=node->depth();i++){
		for(int j=0;j<3;j++){
			for(int k=0;k<3;k++){
				for(int l=0;l<3;l++){
					const TreeOctNode* n=neighborKey2.neighbors[i].neighbors[j][k][l];
					if(n){
						Real temp=n->nodeData.value;
						index[0]=idx[0]+int(n->off[0]);
						index[1]=idx[1]+int(n->off[1]);
						index[2]=idx[2]+int(n->off[2]);
						value+=temp*Real(fData.valueTables[index[0]]*fData.valueTables[index[1]]*fData.valueTables[index[2]]);
						normal.coords[0]+=temp*Real(fData.dValueTables[index[0]]* fData.valueTables[index[1]]* fData.valueTables[index[2]]);
						normal.coords[1]+=temp*Real( fData.valueTables[index[0]]*fData.dValueTables[index[1]]* fData.valueTables[index[2]]);
						normal.coords[2]+=temp*Real( fData.valueTables[index[0]]* fData.valueTables[index[1]]*fData.dValueTables[index[2]]);
					}
				}
			}
		}
	}
	int x,y,z,d=node->depth();
	Cube::FactorCornerIndex(corner,x,y,z);
	for(int i=0;i<2;i++){
		for(int j=0;j<2;j++){
			for(int k=0;k<2;k++){
				const TreeOctNode* n=neighborKey2.neighbors[d].neighbors[x+i][y+j][z+k];
				if(n){
					int ii=Cube::AntipodalCornerIndex(Cube::CornerIndex(i,j,k));
					while(n->children){
						n=&n->children[ii];
						Real temp=n->nodeData.value;
						index[0]=idx[0]+int(n->off[0]);
						index[1]=idx[1]+int(n->off[1]);
						index[2]=idx[2]+int(n->off[2]);
						value+=temp*Real(fData.valueTables[index[0]]*fData.valueTables[index[1]]*fData.valueTables[index[2]]);
						normal.coords[0]+=temp*Real(fData.dValueTables[index[0]]* fData.valueTables[index[1]]* fData.valueTables[index[2]]);
						normal.coords[1]+=temp*Real( fData.valueTables[index[0]]*fData.dValueTables[index[1]]* fData.valueTables[index[2]]);
						normal.coords[2]+=temp*Real( fData.valueTables[index[0]]* fData.valueTables[index[1]]*fData.dValueTables[index[2]]);
					}
				}
			}
		}
	}
}
template<int Degree>
Real Octree<Degree>::GetIsoValue(void){
	if(this->width<=3){
		FILE *fp = fopen("IsoValue.txt", "w");
		TreeOctNode* temp;
		Real isoValue,weightSum,w;

		neighborKey2.set(fData.depth);
		fData.setValueTables(fData.VALUE_FLAG,0);

		isoValue=weightSum=0;
		temp=tree.nextNode();
		while (temp) {
			
			/*if (temp->indexforSample == -1||temp->indexforSample>2264)
			{
				temp = tree.nextNode(temp);
				continue;
			}*/
			
			w=temp->nodeData.centerWeightContribution;
			if(w>EPSILON&&temp->indexforSample<2264&&temp->indexforSample>=0){
				//double CenterValue= getCenterValue(temp)*w;
				//w = 1;
				double CenterValue = getCenterValue(temp)*w;
				isoValue += CenterValue;
				fprintf(fp, "%f\n", CenterValue);
				weightSum+=w;
			}
			temp=tree.nextNode(temp);
		}
		return isoValue/weightSum;
		//return isoValue;
	}
	else{
		const TreeOctNode* temp;
		Real isoValue,weightSum,w;
		Real myRadius;
		PointIndexValueFunction cf;

		fData.setValueTables(fData.VALUE_FLAG,0);
		cf.valueTables=fData.valueTables;
		cf.res2=fData.res2;
		myRadius=radius;
		isoValue=weightSum=0;
		temp=tree.nextNode();
		while(temp){
			w=temp->nodeData.centerWeightContribution;
			if(w>EPSILON){
				cf.value=0;
				int idx[3];
				VertexData::CenterIndex(temp,fData.depth,idx);
				cf.index[0]=idx[0]*fData.res;
				cf.index[1]=idx[1]*fData.res;
				cf.index[2]=idx[2]*fData.res;
				TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,width,&cf);
				isoValue+=cf.value*w;
				weightSum+=w;
			}
			temp=tree.nextNode(temp);
		}
		return isoValue/weightSum;
	}
}
template<int Degree>
void Octree<Degree>::SetIsoSurfaceCorners(const Real& isoValue,const int& subdivideDepth,const int& fullDepthIso){
	double t=Time();
	int i,j;
	hash_map<long long,Real> values;
	Real cornerValues[Cube::CORNERS];
	PointIndexValueFunction cf;
	TreeOctNode* temp;
	int leafCount=tree.leaves();
	long long key;
	SortedTreeNodes *sNodes=new SortedTreeNodes();
	sNodes->set(tree,0);
	temp=tree.nextNode();
	while(temp){
		temp->nodeData.mcIndex=0;
		temp=tree.nextNode(temp);
	}
	TreeNodeData::UseIndex=0;	
	// Start by setting the corner values of all the nodes
	cf.valueTables=fData.valueTables;
	cf.res2=fData.res2;
	for(i=0;i<sNodes->nodeCount[subdivideDepth];i++){
		temp=sNodes->treeNodes[i];
		if(!temp->children){
			for(j=0;j<Cube::CORNERS;j++){
				if(this->width<=3){cornerValues[j]=getCornerValue(temp,j);}
				else{
					cf.value=0;
					int idx[3];
					VertexData::CornerIndex(temp,j,fData.depth,idx);
					cf.index[0]=idx[0]*fData.res;
					cf.index[1]=idx[1]*fData.res;
					cf.index[2]=idx[2]*fData.res;
					TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,width,&cf);
					cornerValues[j]=cf.value;
				}
			}
			temp->nodeData.mcIndex=MarchingCubes::GetIndex(cornerValues,isoValue);

			if(temp->parent){
				TreeOctNode* parent=temp->parent;
				int c=int(temp-temp->parent->children);
				int mcid=temp->nodeData.mcIndex&(1<<MarchingCubes::cornerMap[c]);
				
				if(mcid){
					parent->nodeData.mcIndex|=mcid;
					while(1){
						if(parent->parent && (parent-parent->parent->children)==c){
							parent->parent->nodeData.mcIndex|=mcid;
							parent=parent->parent;
						}
						else{break;}
					}
				}
			}
		}
	}

	MemoryUsage();

	for(i=sNodes->nodeCount[subdivideDepth];i<sNodes->nodeCount[subdivideDepth+1];i++){
		temp=sNodes->treeNodes[i]->nextLeaf();
		while(temp){
			for(j=0;j<Cube::CORNERS;j++){
				int idx[3];
				key=VertexData::CornerIndex(temp,j,fData.depth,idx);
				cf.index[0]=idx[0]*fData.res;
				cf.index[1]=idx[1]*fData.res;
				cf.index[2]=idx[2]*fData.res;
				if(values.find(key)!=values.end()){cornerValues[j]=values[key];}
				else{
					if(this->width<=3){values[key]=cornerValues[j]=getCornerValue(temp,j);}
					else{
						cf.value=0;
						TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,width,&cf);
						values[key]=cf.value;
						cornerValues[j]=cf.value;
					}
				}
			}
			temp->nodeData.mcIndex=MarchingCubes::GetIndex(cornerValues,isoValue);

			if(temp->parent){
				TreeOctNode* parent=temp->parent;
				int c=int(temp-temp->parent->children);
				int mcid=temp->nodeData.mcIndex&(1<<MarchingCubes::cornerMap[c]);
				
				if(mcid){
					parent->nodeData.mcIndex|=mcid;
					while(1){
						if(parent->parent && (parent-parent->parent->children)==c){
							parent->parent->nodeData.mcIndex|=mcid;
							parent=parent->parent;
						}
						else{break;}
					}
				}
			}

			temp=sNodes->treeNodes[i]->nextLeaf(temp);
		}
		MemoryUsage();
		values.clear();
	}
	delete sNodes;
	DumpOutput("Set corner values in: %f\n",Time()-t);
	DumpOutput("Memory Usage: %.3f MB\n",float(MemoryUsage()));

	if(subdivideDepth){PreValidate(isoValue,fData.depth,subdivideDepth);}
}
template<int Degree>
void Octree<Degree>::Subdivide(TreeOctNode* node,const Real& isoValue,const int& maxDepth){
	int i,j,c[4];
	Real value;
	int cornerIndex2[Cube::CORNERS];
	PointIndexValueFunction cf;
	cf.valueTables=fData.valueTables;
	cf.res2=fData.res2;
	node->initChildren();
	// Since we are allocating blocks, it is possible that some of the memory was pre-allocated with
	// the wrong initialization

	// Now set the corner values for the new children
	// Copy old corner values
	for(i=0;i<Cube::CORNERS;i++){cornerIndex2[i]=node->nodeData.mcIndex&(1<<MarchingCubes::cornerMap[i]);}
	// 8 of 27 corners set

	// Set center corner
	cf.value=0;
	int idx[3];
	VertexData::CenterIndex(node,maxDepth,idx);
	cf.index[0]=idx[0]*fData.res;
	cf.index[1]=idx[1]*fData.res;
	cf.index[2]=idx[2]*fData.res;
	if(this->width<=3){value=getCenterValue(node);}
	else{
		TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,width,&cf);
		value=cf.value;
	}
	if(value<isoValue){for(i=0;i<Cube::CORNERS;i++){cornerIndex2[i]|=1<<MarchingCubes::cornerMap[Cube::AntipodalCornerIndex(i)];}}
	// 9 of 27 set

	// Set face corners
	for(i=0;i<Cube::NEIGHBORS;i++){
		int dir,offset,e;
		Cube::FactorFaceIndex(i,dir,offset);
		cf.value=0;
		int idx[3];
		VertexData::FaceIndex(node,i,maxDepth,idx);
		cf.index[0]=idx[0]*fData.res;
		cf.index[1]=idx[1]*fData.res;
		cf.index[2]=idx[2]*fData.res;
		TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,width,&cf);
		value=cf.value;
		Cube::FaceCorners(i,c[0],c[1],c[2],c[3]);
		e=Cube::EdgeIndex(dir,0,0);
		if(value<isoValue){for(j=0;j<4;j++){cornerIndex2[c[j]]|=1<<MarchingCubes::cornerMap[Cube::EdgeReflectCornerIndex(c[j],e)];}}
	}
	// 15 of 27 set

	// Set edge corners
	for(i=0;i<Cube::EDGES;i++){
		int o,i1,i2,f;
		Cube::FactorEdgeIndex(i,o,i1,i2);
		cf.value=0;
		int idx[3];
		VertexData::EdgeIndex(node,i,maxDepth,idx);
		cf.index[0]=idx[0]*fData.res;
		cf.index[1]=idx[1]*fData.res;
		cf.index[2]=idx[2]*fData.res;
		TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,width,&cf);
		value=cf.value;
		Cube::EdgeCorners(i,c[0],c[1]);
		f=Cube::FaceIndex(o,0);
		if(value<isoValue){for(j=0;j<2;j++){cornerIndex2[c[j]]|=1<<MarchingCubes::cornerMap[Cube::FaceReflectCornerIndex(c[j],f)];}}
	}
	// 27 of 27 set

	for(i=0;i<Cube::CORNERS;i++){node->children[i].nodeData.mcIndex=cornerIndex2[i];}
}

template<int Degree>
int Octree<Degree>::InteriorFaceRootCount(const TreeOctNode* node,const int &faceIndex,const int& maxDepth){
	int c1,c2,e1,e2,dir,off,cnt=0;
	int corners[Cube::CORNERS/2];
	if(node->children){
		Cube::FaceCorners(faceIndex,corners[0],corners[1],corners[2],corners[3]);
		Cube::FactorFaceIndex(faceIndex,dir,off);
		c1=corners[0];
		c2=corners[3];
		switch(dir){
			case 0:
				e1=Cube::EdgeIndex(1,off,1);
				e2=Cube::EdgeIndex(2,off,1);
				break;
			case 1:
				e1=Cube::EdgeIndex(0,off,1);
				e2=Cube::EdgeIndex(2,1,off);
				break;
			case 2:
				e1=Cube::EdgeIndex(0,1,off);
				e2=Cube::EdgeIndex(1,1,off);
				break;
		};
		cnt+=EdgeRootCount(&node->children[c1],e1,maxDepth)+EdgeRootCount(&node->children[c1],e2,maxDepth);
		switch(dir){
			case 0:
				e1=Cube::EdgeIndex(1,off,0);
				e2=Cube::EdgeIndex(2,off,0);
				break;
			case 1:
				e1=Cube::EdgeIndex(0,off,0);
				e2=Cube::EdgeIndex(2,0,off);
				break;
			case 2:
				e1=Cube::EdgeIndex(0,0,off);
				e2=Cube::EdgeIndex(1,0,off);
				break;
		};
		cnt+=EdgeRootCount(&node->children[c2],e1,maxDepth)+EdgeRootCount(&node->children[c2],e2,maxDepth);
		for(int i=0;i<Cube::CORNERS/2;i++){if(node->children[corners[i]].children){cnt+=InteriorFaceRootCount(&node->children[corners[i]],faceIndex,maxDepth);}}
	}
	return cnt;
}

template<int Degree>
int Octree<Degree>::EdgeRootCount(const TreeOctNode* node,const int& edgeIndex,const int& maxDepth){
	int f1,f2,c1,c2;
	const TreeOctNode* temp;
	Cube::FacesAdjacentToEdge(edgeIndex,f1,f2);

	int eIndex;
	const TreeOctNode* finest=node;
	eIndex=edgeIndex;
	if(node->depth()<maxDepth){
		temp=node->faceNeighbor(f1);
		if(temp && temp->children){
			finest=temp;
			eIndex=Cube::FaceReflectEdgeIndex(edgeIndex,f1);
		}
		else{
			temp=node->faceNeighbor(f2);
			if(temp && temp->children){
				finest=temp;
				eIndex=Cube::FaceReflectEdgeIndex(edgeIndex,f2);
			}
			else{
				temp=node->edgeNeighbor(edgeIndex);
				if(temp && temp->children){
					finest=temp;
					eIndex=Cube::EdgeReflectEdgeIndex(edgeIndex);
				}
			}
		}
	}

	Cube::EdgeCorners(eIndex,c1,c2);
	if(finest->children){return EdgeRootCount(&finest->children[c1],eIndex,maxDepth)+EdgeRootCount(&finest->children[c2],eIndex,maxDepth);}
	else{return MarchingCubes::HasEdgeRoots(finest->nodeData.mcIndex,eIndex);}
}
template<int Degree>
int Octree<Degree>::IsBoundaryFace(const TreeOctNode* node,const int& faceIndex,const int& subdivideDepth){
	int dir,offset,d,o[3],idx;

	if(subdivideDepth<0){return 0;}
	if(node->d<=subdivideDepth){return 1;}
	Cube::FactorFaceIndex(faceIndex,dir,offset);
	node->depthAndOffset(d,o);

	idx=(int(o[dir])<<1) + (offset<<1);
	return !(idx%(2<<(int(node->d)-subdivideDepth)));
}
template<int Degree>
int Octree<Degree>::IsBoundaryEdge(const TreeOctNode* node,const int& edgeIndex,const int& subdivideDepth){
	int dir,x,y;
	Cube::FactorEdgeIndex(edgeIndex,dir,x,y);
	return IsBoundaryEdge(node,dir,x,y,subdivideDepth);
}
template<int Degree>
int Octree<Degree>::IsBoundaryEdge(const TreeOctNode* node,const int& dir,const int& x,const int& y,const int& subdivideDepth){
	int d,o[3],idx1,idx2,mask;

	if(subdivideDepth<0){return 0;}
	if(node->d<=subdivideDepth){return 1;}
	node->depthAndOffset(d,o);

	switch(dir){
		case 0:
			idx1=(int(o[1])<<1) + (x<<1);
			idx2=(int(o[2])<<1) + (y<<1);
			break;
		case 1:
			idx1=(int(o[0])<<1) + (x<<1);
			idx2=(int(o[2])<<1) + (y<<1);
			break;
		case 2:
			idx1=(int(o[0])<<1) + (x<<1);
			idx2=(int(o[1])<<1) + (y<<1);
			break;
	}
	mask=2<<(int(node->d)-subdivideDepth);
	return !(idx1%(mask)) || !(idx2%(mask));
}

template<int Degree>
void Octree<Degree>::PreValidate(TreeOctNode* node,const Real& isoValue,const int& maxDepth,const int& subdivideDepth){
	int sub=0;
	if(node->children){printf("Bad Pre-Validate\n");}
//	if(int(node->d)<subdivideDepth){sub=1;}
	for(int i=0;i<Cube::NEIGHBORS && !sub;i++){
		TreeOctNode* neighbor=node->faceNeighbor(i);
		if(neighbor && neighbor->children){
			if(IsBoundaryFace(node,i,subdivideDepth) && InteriorFaceRootCount(neighbor,Cube::FaceReflectFaceIndex(i,i),maxDepth)){sub=1;}
		}
	}
	if(sub){
		Subdivide(node,isoValue,maxDepth);
		for(int i=0;i<Cube::NEIGHBORS;i++){
			if(IsBoundaryFace(node,i,subdivideDepth) && InteriorFaceRootCount(node,i,maxDepth)){
				TreeOctNode* neighbor=node->faceNeighbor(i);
				while(neighbor && !neighbor->children){
					PreValidate(neighbor,isoValue,maxDepth,subdivideDepth);
					neighbor=node->faceNeighbor(i);
				}
			}
		}
	}
}

template<int Degree>
void Octree<Degree>::PreValidate(const Real& isoValue,const int& maxDepth,const int& subdivideDepth){
	TreeOctNode* temp;

	temp=tree.nextLeaf();
	while(temp){
		PreValidate(temp,isoValue,maxDepth,subdivideDepth);
		temp=tree.nextLeaf(temp);
	}
}
template<int Degree>
void Octree<Degree>::Validate(TreeOctNode* node,const Real& isoValue,const int& maxDepth,const int& fullDepthIso){
	int i,sub=0;
	TreeOctNode* treeNode=node;
	TreeOctNode* neighbor;
	if(node->depth()>=maxDepth || node->children){return;}

	// Check if full-depth extraction is enabled and we have an iso-node that is not at maximum depth
	if(!sub && fullDepthIso && MarchingCubes::HasRoots(node->nodeData.mcIndex)){sub=1;}

	// Check if the node has faces that are ambiguous and are adjacent to finer neighbors
	for(i=0;i<Cube::NEIGHBORS && !sub;i++){
		neighbor=treeNode->faceNeighbor(i);
		if(neighbor && neighbor->children){if(MarchingCubes::IsAmbiguous(node->nodeData.mcIndex,i)){sub=1;}}
	}

	// Check if the node has edges with more than one root
	for(i=0;i<Cube::EDGES && !sub;i++){if(EdgeRootCount(node,i,maxDepth)>1){sub=1;}}

	for(i=0;i<Cube::NEIGHBORS && !sub;i++){
		neighbor=node->faceNeighbor(i);
		if(	neighbor && neighbor->children &&
			!MarchingCubes::HasFaceRoots(node->nodeData.mcIndex,i) &&
			InteriorFaceRootCount(neighbor,Cube::FaceReflectFaceIndex(i,i),maxDepth)){sub=1;}
	}
	if(sub){
		Subdivide(node,isoValue,maxDepth);
		for(i=0;i<Cube::NEIGHBORS;i++){
			neighbor=treeNode->faceNeighbor(i);
			if(neighbor && !neighbor->children){Validate(neighbor,isoValue,maxDepth,fullDepthIso);}
		}
		for(i=0;i<Cube::EDGES;i++){
			neighbor=treeNode->edgeNeighbor(i);
			if(neighbor && !neighbor->children){Validate(neighbor,isoValue,maxDepth,fullDepthIso);}
		}
		for(i=0;i<Cube::CORNERS;i++){if(!node->children[i].children){Validate(&node->children[i],isoValue,maxDepth,fullDepthIso);}}
	}
}
template<int Degree>
void Octree<Degree>::Validate(TreeOctNode* node,const Real& isoValue,const int& maxDepth,const int& fullDepthIso,const int& subdivideDepth){
	int i,sub=0;
	TreeOctNode* treeNode=node;
	TreeOctNode* neighbor;
	if(node->depth()>=maxDepth || node->children){return;}

	// Check if full-depth extraction is enabled and we have an iso-node that is not at maximum depth
	if(!sub && fullDepthIso && MarchingCubes::HasRoots(node->nodeData.mcIndex)){sub=1;}

	// Check if the node has faces that are ambiguous and are adjacent to finer neighbors
	for(i=0;i<Cube::NEIGHBORS && !sub;i++){
		neighbor=treeNode->faceNeighbor(i);
		if(neighbor && neighbor->children){if(MarchingCubes::IsAmbiguous(node->nodeData.mcIndex,i) || IsBoundaryFace(node,i,subdivideDepth)){sub=1;}}
	}

	// Check if the node has edges with more than one root
	for(i=0;i<Cube::EDGES && !sub;i++){if(EdgeRootCount(node,i,maxDepth)>1){sub=1;}}

	for(i=0;i<Cube::NEIGHBORS && !sub;i++){
		neighbor=node->faceNeighbor(i);
		if(	neighbor && neighbor->children && !MarchingCubes::HasFaceRoots(node->nodeData.mcIndex,i) &&
			InteriorFaceRootCount(neighbor,Cube::FaceReflectFaceIndex(i,i),maxDepth)){sub=1;}
	}
	if(sub){
		Subdivide(node,isoValue,maxDepth);
		for(i=0;i<Cube::NEIGHBORS;i++){
			neighbor=treeNode->faceNeighbor(i);
			if(neighbor && !neighbor->children){Validate(neighbor,isoValue,maxDepth,fullDepthIso,subdivideDepth);}
		}
		for(i=0;i<Cube::EDGES;i++){
			neighbor=treeNode->edgeNeighbor(i);
			if(neighbor && !neighbor->children){Validate(neighbor,isoValue,maxDepth,fullDepthIso,subdivideDepth);}
		}
		for(i=0;i<Cube::CORNERS;i++){if(!node->children[i].children){Validate(&node->children[i],isoValue,maxDepth,fullDepthIso,subdivideDepth);}}
	}
}
//////////////////////////////////////////////////////////////////////////////////////
// The assumption made when calling this code is that the edge has at most one root //
//////////////////////////////////////////////////////////////////////////////////////
template<int Degree>
int Octree<Degree>::GetRoot(const RootInfo& ri,const Real& isoValue,Point3D<Real> & position,hash_map<long long,std::pair<Real,Point3D<Real> > >& normalHash,const int& nonLinearFit){
	int c1,c2;
	Cube::EdgeCorners(ri.edgeIndex,c1,c2);
	if(!MarchingCubes::HasEdgeRoots(ri.node->nodeData.mcIndex,ri.edgeIndex)){return 0;}

	long long key;
	Point3D<Real> n[2];
	PointIndexValueAndNormalFunction cnf;
	cnf.valueTables=fData.valueTables;
	cnf.dValueTables=fData.dValueTables;
	cnf.res2=fData.res2;

	int i,o,i1,i2,rCount=0;
	Polynomial<2> P;
	std::vector<double> roots;
	double x0,x1;
	Real center,width;
	Real averageRoot=0;
	Cube::FactorEdgeIndex(ri.edgeIndex,o,i1,i2);
	int idx[3];
	key=VertexData::CornerIndex(ri.node,c1,fData.depth,idx);
	cnf.index[0]=idx[0]*fData.res;
	cnf.index[1]=idx[1]*fData.res;
	cnf.index[2]=idx[2]*fData.res;

	if(normalHash.find(key)==normalHash.end()){
		cnf.value=0;
		cnf.normal.coords[0]=cnf.normal.coords[1]=cnf.normal.coords[2]=0;
		// Careful here as the normal isn't quite accurate... (i.e. postNormalSmooth is ignored)
#if 0
		if(this->width<=3){getCornerValueAndNormal(ri.node,c1,cnf.value,cnf.normal);}
		else{TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,this->width,&cnf);}
#else
		TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,this->width,&cnf);
#endif
		normalHash[key]=std::pair<Real,Point3D<Real> >(cnf.value,cnf.normal);
	}
	x0=normalHash[key].first;
	n[0]=normalHash[key].second;

	key=VertexData::CornerIndex(ri.node,c2,fData.depth,idx);
	cnf.index[0]=idx[0]*fData.res;
	cnf.index[1]=idx[1]*fData.res;
	cnf.index[2]=idx[2]*fData.res;
	if(normalHash.find(key)==normalHash.end()){
		cnf.value=0;
		cnf.normal.coords[0]=cnf.normal.coords[1]=cnf.normal.coords[2]=0;
#if 0
		if(this->width<=3){getCornerValueAndNormal(ri.node,c2,cnf.value,cnf.normal);}
		else{TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,this->width,&cnf);}
#else
		TreeOctNode::ProcessPointAdjacentNodes(fData.depth,idx,&tree,this->width,&cnf);
#endif
		normalHash[key]=std::pair<Real,Point3D<Real> >(cnf.value,cnf.normal);
	}
	x1=normalHash[key].first;
	n[1]=normalHash[key].second;

	Point3D<Real> c;
	ri.node->centerAndWidth(c,width);
	center=c.coords[o];
	for(i=0;i<DIMENSION;i++){
		n[0].coords[i]*=width;
		n[1].coords[i]*=width;
	}

	switch(o){
				case 0:
					position.coords[1]=c.coords[1]-width/2+width*i1;
					position.coords[2]=c.coords[2]-width/2+width*i2;
					break;
				case 1:
					position.coords[0]=c.coords[0]-width/2+width*i1;
					position.coords[2]=c.coords[2]-width/2+width*i2;
					break;
				case 2:
					position.coords[0]=c.coords[0]-width/2+width*i1;
					position.coords[1]=c.coords[1]-width/2+width*i2;
					break;
	}
	double dx0,dx1;
	dx0=n[0].coords[o];
	dx1=n[1].coords[o];

	// The scaling will turn the Hermite Spline into a quadratic
	double scl=(x1-x0)/((dx1+dx0)/2);
	dx0*=scl;
	dx1*=scl;

	// Hermite Spline
	P.coefficients[0]=x0;
	P.coefficients[1]=dx0;
	P.coefficients[2]=3*(x1-x0)-dx1-2*dx0;

	P.getSolutions(isoValue,roots,EPSILON);
	for(i=0;i<int(roots.size());i++){
		if(roots[i]>=0 && roots[i]<=1){
			averageRoot+=Real(roots[i]);
			rCount++;
		}
	}
	if(rCount && nonLinearFit)	{averageRoot/=rCount;}
	else						{averageRoot=Real((x0-isoValue)/(x0-x1));}

	position.coords[o]=Real(center-width/2+width*averageRoot);
	return 1;
}

template<int Degree>
int Octree<Degree>::GetRoot(const RootInfo& ri,const Real& isoValue,const int& maxDepth,Point3D<Real>& position,hash_map<long long,std::pair<Real,Point3D<Real> > >& normals,
							Point3D<Real>* normal,const int& nonLinearFit){
	if(!MarchingCubes::HasRoots(ri.node->nodeData.mcIndex)){return 0;}
	return GetRoot(ri,isoValue,position,normals,nonLinearFit);
}
template<int Degree>
int Octree<Degree>::GetRootIndex(const TreeOctNode* node,const int& edgeIndex,const int& maxDepth,const int& sDepth,RootInfo& ri){
	int c1,c2,f1,f2;
	const TreeOctNode *temp,*finest;
	int finestIndex;

	Cube::FacesAdjacentToEdge(edgeIndex,f1,f2);

	finest=node;
	finestIndex=edgeIndex;
	if(node->depth()<maxDepth){
		if(IsBoundaryFace(node,f1,sDepth)){temp=NULL;}
		else{temp=node->faceNeighbor(f1);}
		if(temp && temp->children){
			finest=temp;
			finestIndex=Cube::FaceReflectEdgeIndex(edgeIndex,f1);
		}
		else{
			if(IsBoundaryFace(node,f2,sDepth)){temp=NULL;}
			else{temp=node->faceNeighbor(f2);}
			if(temp && temp->children){
				finest=temp;
				finestIndex=Cube::FaceReflectEdgeIndex(edgeIndex,f2);
			}
			else{
				if(IsBoundaryEdge(node,edgeIndex,sDepth)){temp=NULL;}
				else{temp=node->edgeNeighbor(edgeIndex);}
				if(temp && temp->children){
					finest=temp;
					finestIndex=Cube::EdgeReflectEdgeIndex(edgeIndex);
				}
			}
		}
	}

	Cube::EdgeCorners(finestIndex,c1,c2);
	if(finest->children){
		if		(GetRootIndex(&finest->children[c1],finestIndex,maxDepth,sDepth,ri))	{return 1;}
		else if	(GetRootIndex(&finest->children[c2],finestIndex,maxDepth,sDepth,ri))	{return 1;}
		else																							{return 0;}
	}
	else{
		if(!(MarchingCubes::edgeMask[finest->nodeData.mcIndex] & (1<<finestIndex))){return 0;}

		int o,i1,i2;
		Cube::FactorEdgeIndex(finestIndex,o,i1,i2);
		int d,off[3];
		finest->depthAndOffset(d,off);
		ri.node=finest;
		ri.edgeIndex=finestIndex;
		int eIndex[2],offset;
		offset=BinaryNode<Real>::Index(d,off[o]);
		switch(o){
				case 0:
					eIndex[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[1],i1);
					eIndex[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[2],i2);
					break;
				case 1:
					eIndex[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[0],i1);
					eIndex[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[2],i2);
					break;
				case 2:
					eIndex[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[0],i1);
					eIndex[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[1],i2);
					break;
		}
		ri.key = (long long)(o) | (long long)(eIndex[0])<<5 | (long long)(eIndex[1])<<25 | (long long)(offset)<<45;
		return 1;
	}
}
template<int Degree>
int Octree<Degree>::GetRootIndex(const TreeOctNode* node,const int& edgeIndex,const int& maxDepth,RootInfo& ri){
	int c1,c2,f1,f2;
	const TreeOctNode *temp,*finest;
	int finestIndex;


	// The assumption is that the super-edge has a root along it. 
	if(!(MarchingCubes::edgeMask[node->nodeData.mcIndex] & (1<<edgeIndex))){return 0;}

	Cube::FacesAdjacentToEdge(edgeIndex,f1,f2);

	finest=node;
	finestIndex=edgeIndex;
	if(node->depth()<maxDepth){
		temp=node->faceNeighbor(f1);
		if(temp && temp->children){
			finest=temp;
			finestIndex=Cube::FaceReflectEdgeIndex(edgeIndex,f1);
		}
		else{
			temp=node->faceNeighbor(f2);
			if(temp && temp->children){
				finest=temp;
				finestIndex=Cube::FaceReflectEdgeIndex(edgeIndex,f2);
			}
			else{
				temp=node->edgeNeighbor(edgeIndex);
				if(temp && temp->children){
					finest=temp;
					finestIndex=Cube::EdgeReflectEdgeIndex(edgeIndex);
				}
			}
		}
	}

	Cube::EdgeCorners(finestIndex,c1,c2);
	if(finest->children){
		if		(GetRootIndex(&finest->children[c1],finestIndex,maxDepth,ri))				{return 1;}
		else if	(GetRootIndex(&finest->children[c2],finestIndex,maxDepth,ri))				{return 1;}
		else																				{return 0;}
	}
	else{
		int o,i1,i2;
		Cube::FactorEdgeIndex(finestIndex,o,i1,i2);
		int d,off[3];
		finest->depthAndOffset(d,off);
		ri.node=finest;
		ri.edgeIndex=finestIndex;
		int offset,eIndex[2];
		offset=BinaryNode<Real>::Index(d,off[o]);
		switch(o){
				case 0:
					eIndex[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[1],i1);
					eIndex[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[2],i2);
					break;
				case 1:
					eIndex[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[0],i1);
					eIndex[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[2],i2);
					break;
				case 2:
					eIndex[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[0],i1);
					eIndex[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[1],i2);
					break;
		}
		ri.key= (long long)(o) | (long long)(eIndex[0])<<5 | (long long)(eIndex[1])<<25 | (long long)(offset)<<45;
		return 1;
	}
}
template<int Degree>
int Octree<Degree>::GetRootPair(const RootInfo& ri,const int& maxDepth,RootInfo& pair){
	const TreeOctNode* node=ri.node;
	int c1,c2,c;
	Cube::EdgeCorners(ri.edgeIndex,c1,c2);
	while(node->parent){
		c=int(node-node->parent->children);
		if(c!=c1 && c!=c2){return 0;}
		if(!MarchingCubes::HasEdgeRoots(node->parent->nodeData.mcIndex,ri.edgeIndex)){
			if(c==c1){return GetRootIndex(&node->parent->children[c2],ri.edgeIndex,maxDepth,pair);}
			else{return GetRootIndex(&node->parent->children[c1],ri.edgeIndex,maxDepth,pair);}
		}
		node=node->parent;
	}
	return 0;

}
template<int Degree>
int Octree<Degree>::GetRootIndex(const long long& key,hash_map<long long,int>& boundaryRoots,hash_map<long long,int>* interiorRoots,CoredPointIndex& index){
	hash_map<long long,int>::iterator rootIter=boundaryRoots.find(key);
	if(rootIter!=boundaryRoots.end()){
		index.inCore=1;
		index.index=rootIter->second;
		return 1;
	}
	else if(interiorRoots){
		rootIter=interiorRoots->find(key);
		if(rootIter!=interiorRoots->end()){
			index.inCore=0;
			index.index=rootIter->second;
			return 1;
		}
	}
	return 0;
}
template<int Degree>
int Octree<Degree>::SetMCRootPositions(TreeOctNode* node,const int& sDepth,const Real& isoValue,
									   hash_map<long long,int>& boundaryRoots,hash_map<long long,int>* interiorRoots,
									   hash_map<long long,std::pair<Real,Point3D<Real> > >& boundaryNormalHash,hash_map<long long,std::pair<Real,Point3D<Real> > >* interiorNormalHash,
									   std::vector<Point3D<float> >* interiorPositions,
									   CoredMeshData* mesh,const int& nonLinearFit){
	Point3D<Real> position;
	int i,j,k,eIndex;
	RootInfo ri;
	int count=0;
	if(!MarchingCubes::HasRoots(node->nodeData.mcIndex)){return 0;}
	for(i=0;i<DIMENSION;i++){
		for(j=0;j<2;j++){
			for(k=0;k<2;k++){
				long long key;
				eIndex=Cube::EdgeIndex(i,j,k);
				if(GetRootIndex(node,eIndex,fData.depth,ri)){
					key=ri.key;
					if(!interiorRoots || IsBoundaryEdge(node,i,j,k,sDepth)){
						if(boundaryRoots.find(key)==boundaryRoots.end()){
							GetRoot(ri,isoValue,fData.depth,position,boundaryNormalHash,NULL,nonLinearFit);
							mesh->inCorePoints.push_back(position);
							boundaryRoots[key]=int(mesh->inCorePoints.size())-1;
							count++;
						}
					}
					else{
						if(interiorRoots->find(key)==interiorRoots->end()){
							GetRoot(ri,isoValue,fData.depth,position,*interiorNormalHash,NULL,nonLinearFit);
							(*interiorRoots)[key]=mesh->addOutOfCorePoint(position);
							interiorPositions->push_back(position);
							count++;
						}
					}
				}
			}
		}
	}
	return count;
}
template<int Degree>
int Octree<Degree>::SetBoundaryMCRootPositions(const int& sDepth,const Real& isoValue,
											   hash_map<long long,int>& boundaryRoots,hash_map<long long,std::pair<Real,Point3D<Real> > >& boundaryNormalHash,
											   CoredMeshData* mesh,const int& nonLinearFit){
	Point3D<Real> position;
	int i,j,k,eIndex,hits=0;
	RootInfo ri;
	int count=0;
	TreeOctNode* node;

	node=tree.nextLeaf();
	while(node){
		if(MarchingCubes::HasRoots(node->nodeData.mcIndex)){
			hits=0;
			for(i=0;i<DIMENSION;i++){
				for(j=0;j<2;j++){
					for(k=0;k<2;k++){
						if(IsBoundaryEdge(node,i,j,k,sDepth)){
							hits++;
							long long key;
							eIndex=Cube::EdgeIndex(i,j,k);
							if(GetRootIndex(node,eIndex,fData.depth,ri)){
								key=ri.key;
								if(boundaryRoots.find(key)==boundaryRoots.end()){
									GetRoot(ri,isoValue,fData.depth,position,boundaryNormalHash,NULL,nonLinearFit);
									mesh->inCorePoints.push_back(position);
									boundaryRoots[key]=int(mesh->inCorePoints.size())-1;
									count++;
								}
							}
						}
					}
				}
			}
		}
		if(hits){node=tree.nextLeaf(node);}
		else{node=tree.nextBranch(node);}
	}
	return count;
}
template<int Degree>
void Octree<Degree>::GetMCIsoEdges(TreeOctNode* node,hash_map<long long,int>& boundaryRoots,hash_map<long long,int>* interiorRoots,const int& sDepth,
								   std::vector<std::pair<long long,long long> >& edges){
	TreeOctNode* temp;
	int count=0,tris=0;
	int isoTri[DIMENSION*MarchingCubes::MAX_TRIANGLES];
	FaceEdgesFunction fef;
	int ref,fIndex;
	hash_map<long long,std::pair<RootInfo,int> >::iterator iter;
	hash_map<long long,std::pair<RootInfo,int> > vertexCount;

	fef.edges=&edges;
	fef.maxDepth=fData.depth;
	fef.vertexCount=&vertexCount;
	count=MarchingCubes::AddTriangleIndices(node->nodeData.mcIndex,isoTri);
	for(fIndex=0;fIndex<Cube::NEIGHBORS;fIndex++){
		ref=Cube::FaceReflectFaceIndex(fIndex,fIndex);
		fef.fIndex=ref;
		temp=node->faceNeighbor(fIndex);
		// If the face neighbor exists and has higher resolution than the current node,
		// get the iso-curve from the neighbor
		if(temp && temp->children && !IsBoundaryFace(node,fIndex,sDepth)){temp->processNodeFaces(temp,&fef,ref);}
		// Otherwise, get it from the node
		else{
			RootInfo ri1,ri2;
			for(int j=0;j<count;j++){
				for(int k=0;k<3;k++){
					if(fIndex==Cube::FaceAdjacentToEdges(isoTri[j*3+k],isoTri[j*3+((k+1)%3)])){
						if(GetRootIndex(node,isoTri[j*3+k],fData.depth,ri1) && GetRootIndex(node,isoTri[j*3+((k+1)%3)],fData.depth,ri2)){
							edges.push_back(std::pair<long long,long long>(ri1.key,ri2.key));
							iter=vertexCount.find(ri1.key);
							if(iter==vertexCount.end()){
								vertexCount[ri1.key].first=ri1;
								vertexCount[ri1.key].second=0;
							}
							iter=vertexCount.find(ri2.key);
							if(iter==vertexCount.end()){
								vertexCount[ri2.key].first=ri2;
								vertexCount[ri2.key].second=0;
							}
							vertexCount[ri1.key].second++;
							vertexCount[ri2.key].second--;
						}
						else{fprintf(stderr,"Bad Edge 1: %d %d\n",ri1.key,ri2.key);}
					}
				}
			}
		}
	}
	for(int i=0;i<int(edges.size());i++){
		iter=vertexCount.find(edges[i].first);
		if(iter==vertexCount.end()){printf("Could not find vertex: %lld\n",edges[i].first);}
		else if(vertexCount[edges[i].first].second){
			RootInfo ri;
			GetRootPair(vertexCount[edges[i].first].first,fData.depth,ri);
			iter=vertexCount.find(ri.key);
			if(iter==vertexCount.end()){printf("Vertex pair not in list\n");}
			else{
				edges.push_back(std::pair<long long,long long>(ri.key,edges[i].first));
				vertexCount[ri.key].second++;
				vertexCount[edges[i].first].second--;
			}
		}

		iter=vertexCount.find(edges[i].second);
		if(iter==vertexCount.end()){printf("Could not find vertex: %lld\n",edges[i].second);}
		else if(vertexCount[edges[i].second].second){
			RootInfo ri;
			GetRootPair(vertexCount[edges[i].second].first,fData.depth,ri);
			iter=vertexCount.find(ri.key);
			if(iter==vertexCount.end()){printf("Vertex pair not in list\n");}
			else{
				edges.push_back(std::pair<long long,long long>(edges[i].second,ri.key));
				vertexCount[edges[i].second].second++;
				vertexCount[ri.key].second--;
			}
		}
	}
}
template<int Degree>
int Octree<Degree>::GetMCIsoTriangles(TreeOctNode* node,CoredMeshData* mesh,hash_map<long long,int>& boundaryRoots,
									  hash_map<long long,int>* interiorRoots,std::vector<Point3D<float> >* interiorPositions,const int& offSet,const int& sDepth , bool addBarycenter , bool polygonMesh )
{
	int tris=0;
	std::vector<std::pair<long long,long long> > edges;
	std::vector<std::vector<std::pair<long long,long long> > > edgeLoops;
	GetMCIsoEdges(node,boundaryRoots,interiorRoots,sDepth,edges);

	GetEdgeLoops(edges,edgeLoops);
	for(int i=0;i<int(edgeLoops.size());i++){
		CoredPointIndex p;
		std::vector<CoredPointIndex> edgeIndices;
		for(int j=0;j<int(edgeLoops[i].size());j++){
			if(!GetRootIndex(edgeLoops[i][j].first,boundaryRoots,interiorRoots,p)){printf("Bad Point Index\n");}
			else{edgeIndices.push_back(p);}
		}
		tris+=AddTriangles(mesh,edgeIndices,interiorPositions,offSet , addBarycenter , polygonMesh );
	}
	return tris;
}

template<int Degree>
int Octree<Degree>::GetEdgeLoops(std::vector<std::pair<long long,long long> >& edges,std::vector<std::vector<std::pair<long long,long long> > >& loops){
	int loopSize=0;
	long long frontIdx,backIdx;
	std::pair<long long,long long> e,temp;
	loops.clear();

	while(edges.size()){
		std::vector<std::pair<long long,long long> > front,back;
		e=edges[0];
		loops.resize(loopSize+1);
		edges[0]=edges[edges.size()-1];
		edges.pop_back();
		frontIdx=e.second;
		backIdx=e.first;
		for(int j=int(edges.size())-1;j>=0;j--){
			if(edges[j].first==frontIdx || edges[j].second==frontIdx){
				if(edges[j].first==frontIdx)	{temp=edges[j];}
				else							{temp.first=edges[j].second;temp.second=edges[j].first;}
				frontIdx=temp.second;
				front.push_back(temp);
				edges[j]=edges[edges.size()-1];
				edges.pop_back();
				j=int(edges.size());
			}
			else if(edges[j].first==backIdx || edges[j].second==backIdx){
				if(edges[j].second==backIdx)	{temp=edges[j];}
				else							{temp.first=edges[j].second;temp.second=edges[j].first;}
				backIdx=temp.first;
				back.push_back(temp);
				edges[j]=edges[edges.size()-1];
				edges.pop_back();
				j=int(edges.size());
			}
		}
		for(int j=int(back.size())-1;j>=0;j--){loops[loopSize].push_back(back[j]);}
		loops[loopSize].push_back(e);
		for(int j=0;j<int(front.size());j++){loops[loopSize].push_back(front[j]);}
		loopSize++;
	}
	return int(loops.size());
}
template<int Degree>
int Octree<Degree>::AddTriangles(CoredMeshData* mesh,std::vector<CoredPointIndex> edges[3],std::vector<Point3D<float> >* interiorPositions,const int& offSet){
	std::vector<CoredPointIndex> e;
	for(int i=0;i<3;i++){for(size_t j=0;j<edges[i].size();j++){e.push_back(edges[i][j]);}}
	return AddTriangles(mesh,e,interiorPositions,offSet);
}
template<int Degree>
int Octree<Degree>::AddTriangles( CoredMeshData* mesh , std::vector<CoredPointIndex>& edges , std::vector<Point3D<float> >* interiorPositions , const int& offSet , bool addBarycenter , bool polygonMesh )
{
	if( polygonMesh )
	{
		std::vector< CoredVertexIndex > vertices( edges.size() );
		for( int i=0 ; i<edges.size() ; i++ )
		{
			vertices[i].idx    = edges[i].index;
			vertices[i].inCore = edges[i].inCore;
		}
		mesh->addPolygon( vertices );
		return 1;
	}
	if( edges.size()>3 )
	{
#if 1
		bool isCoplanar = false;

		for( int i=0 ; i<edges.size() ; i++ )
			for( int j=0 ; j<i ; j++ )
				if( (i+1)%edges.size()!=j && (j+1)%edges.size()!=i )
				{
					Point3D< Real > v1 , v2;
					if( edges[i].inCore ) for( int k=0 ; k<3 ; k++ ) v1.coords[k] =   mesh->inCorePoints[ edges[i].index        ].coords[k];
					else                  for( int k=0 ; k<3 ; k++ ) v1.coords[k] = (*interiorPositions)[ edges[i].index-offSet ].coords[k];
					if( edges[j].inCore ) for( int k=0 ; k<3 ; k++ ) v2.coords[k] =   mesh->inCorePoints[ edges[j].index        ].coords[k];
					else                  for( int k=0 ; k<3 ; k++ ) v2.coords[k] = (*interiorPositions)[ edges[j].index-offSet ].coords[k];
					for( int k=0 ; k<3 ; k++ ) if( v1.coords[k]==v2.coords[k] ) isCoplanar = true;
				}
		if( addBarycenter && isCoplanar )
#else
		if( addBarycenter )
#endif
		{
			Point3D< Real > c;
			c.coords[0] = c.coords[1] = c.coords[2] = 0;
			for( int i=0 ; i<int(edges.size()) ; i++ )
			{
				Point3D<Real> p;
				if(edges[i].inCore)	for(int j=0 ; j<3 ; j++ ) p.coords[j] =  mesh->inCorePoints[edges[i].index].coords[j];
				else				for(int j=0 ; j<3 ; j++ ) p.coords[j] =(*interiorPositions)[edges[i].index-offSet].coords[j];
				c.coords[0] += p.coords[0] , c.coords[1] += p.coords[1] , c.coords[2] += p.coords[2];
			}
			c.coords[0] /= edges.size() , c.coords[1] /= edges.size() , c.coords[2] /= edges.size();
			int cIdx = mesh->addOutOfCorePoint( c );
			for( int i=0 ; i<int(edges.size()) ; i++ )
			{
				std::vector< CoredVertexIndex > vertices( 3 );
				vertices[0].idx = edges[i].index;
				vertices[1].idx = edges[(i+1)%edges.size()].index;
				vertices[2].idx = cIdx;
				vertices[0].inCore = edges[i                 ].inCore;
				vertices[1].inCore = edges[(i+1)%edges.size()].inCore;
				vertices[2].inCore = 0;
				mesh->addPolygon( vertices );
			}
			return edges.size();
		}
		else
		{
			Triangulation<float> t;

			// Add the points to the triangulation
			for(int i=0;i<int(edges.size());i++){
				Point3D<Real> p;
				if(edges[i].inCore)	{for(int j=0;j<3;j++){p.coords[j]=mesh->inCorePoints[edges[i].index].coords[j];}}
				else				{for(int j=0;j<3;j++){p.coords[j]=(*interiorPositions)[edges[i].index-offSet].coords[j];}}
				t.points.push_back(p);
			}

			// Create a fan triangulation
			for(int i=1;i<int(edges.size())-1;i++){t.addTriangle(0,i,i+1);}

			// Minimize
			while(1){
				int i;
				for(i=0;i<int(t.edges.size());i++){if(t.flipMinimize(i)){break;}}
				if(i==t.edges.size()){break;}
			}
			// Add the triangles to the mesh
			for(int i=0;i<int(t.triangles.size());i++)
			{
				std::vector< CoredVertexIndex > vertices( 3 );
				int idx[3];
				t.factor( i , idx[0] , idx[1] , idx[2] );
				for( int j=0 ; j<3 ; j++ )
				{
					vertices[j].idx = edges[ idx[j] ].index;
					vertices[j].inCore = edges[ idx[j] ].inCore;
				}
				mesh->addPolygon( vertices );
			}
		}
	}
	else if( edges.size()==3 )
	{
		std::vector< CoredVertexIndex > vertices( 3 );
		for( int i=0 ; i<3 ; i++ )
		{
			vertices[i].idx = edges[i].index;
			vertices[i].inCore = edges[i].inCore;
		}
		mesh->addPolygon( vertices );
	}
	return int(edges.size())-2;
}
////////////////
// VertexData //
////////////////
long long VertexData::CenterIndex(const TreeOctNode* node,const int& maxDepth){
	int idx[DIMENSION];
	return CenterIndex(node,maxDepth,idx);
}
long long VertexData::CenterIndex(const TreeOctNode* node,const int& maxDepth,int idx[DIMENSION]){
	int d,o[3];
	node->depthAndOffset(d,o);
	for(int i=0;i<DIMENSION;i++){idx[i]=BinaryNode<Real>::CornerIndex(maxDepth+1,d+1,o[i]<<1,1);}
	return (long long)(idx[0]) | (long long)(idx[1])<<15 | (long long)(idx[2])<<30;
}
long long VertexData::CenterIndex(const int& depth,const int offSet[DIMENSION],const int& maxDepth,int idx[DIMENSION]){
	for(int i=0;i<DIMENSION;i++){idx[i]=BinaryNode<Real>::CornerIndex(maxDepth+1,depth+1,offSet[i]<<1,1);}
	return (long long)(idx[0]) | (long long)(idx[1])<<15 | (long long)(idx[2])<<30;
}
long long VertexData::CornerIndex(const TreeOctNode* node,const int& cIndex,const int& maxDepth){
	int idx[DIMENSION];
	return CornerIndex(node,cIndex,maxDepth,idx);
}
long long VertexData::CornerIndex(const TreeOctNode* node,const int& cIndex,const int& maxDepth,int idx[DIMENSION]){
	int x[DIMENSION];
	Cube::FactorCornerIndex(cIndex,x[0],x[1],x[2]);
	int d,o[3];
	node->depthAndOffset(d,o);
	for(int i=0;i<DIMENSION;i++){idx[i]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,o[i],x[i]);}
	return (long long)(idx[0]) | (long long)(idx[1])<<15 | (long long)(idx[2])<<30;
}
long long VertexData::CornerIndex(const int& depth,const int offSet[DIMENSION],const int& cIndex,const int& maxDepth,int idx[DIMENSION]){
	int x[DIMENSION];
	Cube::FactorCornerIndex(cIndex,x[0],x[1],x[2]);
	for(int i=0;i<DIMENSION;i++){idx[i]=BinaryNode<Real>::CornerIndex(maxDepth+1,depth,offSet[i],x[i]);}
	return (long long)(idx[0]) | (long long)(idx[1])<<15 | (long long)(idx[2])<<30;
}
long long VertexData::FaceIndex(const TreeOctNode* node,const int& fIndex,const int& maxDepth){
	int idx[DIMENSION];
	return FaceIndex(node,fIndex,maxDepth,idx);
}
long long VertexData::FaceIndex(const TreeOctNode* node,const int& fIndex,const int& maxDepth,int idx[DIMENSION]){
	int dir,offset;
	Cube::FactorFaceIndex(fIndex,dir,offset);
	int d,o[3];
	node->depthAndOffset(d,o);
	for(int i=0;i<DIMENSION;i++){idx[i]=BinaryNode<Real>::CornerIndex(maxDepth+1,d+1,o[i]<<1,1);}
	idx[dir]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,o[dir],offset);
	return (long long)(idx[0]) | (long long)(idx[1])<<15 | (long long)(idx[2])<<30;
}
long long VertexData::EdgeIndex(const TreeOctNode* node,const int& eIndex,const int& maxDepth){
	int idx[DIMENSION];
	return EdgeIndex(node,eIndex,maxDepth,idx);
}
long long VertexData::EdgeIndex(const TreeOctNode* node,const int& eIndex,const int& maxDepth,int idx[DIMENSION]){
	int o,i1,i2;
	int d,off[3];
	node->depthAndOffset(d,off);
	for(int i=0;i<DIMENSION;i++){idx[i]=BinaryNode<Real>::CornerIndex(maxDepth+1,d+1,off[i]<<1,1);}
	Cube::FactorEdgeIndex(eIndex,o,i1,i2);
	switch(o){
		case 0:
			idx[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[1],i1);
			idx[2]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[2],i2);
			break;
		case 1:
			idx[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[0],i1);
			idx[2]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[2],i2);
			break;
		case 2:
			idx[0]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[0],i1);
			idx[1]=BinaryNode<Real>::CornerIndex(maxDepth+1,d,off[1],i2);
			break;
	};
	return (long long)(idx[0]) | (long long)(idx[1])<<15 | (long long)(idx[2])<<30;
}
template<int Degree>
void Octree<Degree>::write_ply(float *triangles, int data_length, char *output_file, int *mcIsLeaf) {

	std::fstream plyfile;
	plyfile.open(output_file, std::fstream::out);
	printf("Writing\n");
	//std::cout << "Writing\n" << endl;
	plyfile << "ply\nformat ascii 1.0\n";
	plyfile << "element vertex                \n"; // need to come back and add amount of vertices
	plyfile << "property float x\nproperty float y\nproperty float z\n";
	plyfile << "element face                \n";// need to come back here and add amount of faces
	plyfile << "property list uchar int vertex_index\n";
	plyfile << "end_header\n";
	int i = 0;
	int edge_num = 0;
	while (i<data_length - 3) {
		//printf("%d \n",i );
		if ((abs(triangles[i]) >10e-3)) {
			/*if ((i%9==0) && i<data_length-10 && abs(triangles[i+1]) > 0.37 && triangles[i+4] >=  0.37 && triangles[i+7] >= 0.37)
			{
			i += 8;
			}
			else*/
			{
				plyfile << triangles[i] << " " << triangles[i + 1] << " " << triangles[i + 2] << "\n";
				i += 2;
				edge_num++;
			}
		}
		i++;
	}
	printf("edge_num %d\n", edge_num);
	//std::cout << "edge_num" << edge_num << endl;
	int f = 0;
	int x = 0;
	while (f < edge_num / 3) {
		plyfile << "3 " << x << " " << x + 1 << " " << x + 2 << "\n";
		x += 3;
		f++;
	}
	plyfile.clear();
	plyfile.seekg(38, std::ios::beg);
	plyfile << edge_num;
	plyfile.seekg(122, std::ios::beg);
	plyfile << edge_num / 3;
	plyfile.seekg(0, std::ios::end);

	plyfile.close();

	printf("face num: %d\n", edge_num / 3);


}
