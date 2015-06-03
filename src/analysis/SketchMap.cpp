/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2012 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed-code.org for more information.

   This file is part of plumed, version 2.0.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "DimensionalityReductionBase.h"
#include "ClassicalScaling.h"
#include "SMACOF.h"
#include "tools/ConjugateGradient.h"
#include "tools/GridSearch.h"
#include "reference/PointWiseMapping.h"
#include "core/ActionRegister.h"
#include "tools/SwitchingFunction.h"
#include <iostream>
#include <algorithm> 
#include <math.h>
#include <iostream>
#include <fstream>
#include <sstream>
//+PLUMEDOC ANALYSIS SKETCHMAP
/*
Perform a dimensionality reduction using the sketch-map algorithm

\par Examples

*/
//+ENDPLUMEDOC

namespace PLMD {
namespace analysis {

class SketchMap : public DimensionalityReductionBase {
private:
  bool nosmacof;
  double smactol, smaptol, regulariser;
  SwitchingFunction lowdf, highdf;
  double recalculateWeights( const Matrix<double>& Distances, const Matrix<double>& F, PointWiseMapping* mymap, Matrix<double>& Weights );
public:
  static void registerKeywords( Keywords& keys );
  SketchMap( const ActionOptions& ao );
  std::string getAlgorithmName() const { return "sketch-map"; }
  void calculateAllDistances( PointWiseMapping* mymap, Matrix<double>& targets );
  void generateProjections( PointWiseMapping* mymap );
  double transformHD( const double& val, double& df ) const ;
  double transformLD( const double& val, double& df ) const ;
};

PLUMED_REGISTER_ACTION(SketchMap,"SKETCHMAP")

void SketchMap::registerKeywords( Keywords& keys ){
  DimensionalityReductionBase::registerKeywords( keys );
  keys.add("compulsory","HIGH_DIM_FUNCTION","the parameters of the switching function in the high dimensional space");
  keys.add("compulsory","LOW_DIM_FUNCTION","the parameters of the switching function in the low dimensional space");
  keys.add("compulsory","SMACOF_TOL","1E-4","the tolerance for each SMACOF cycle");
  keys.add("compulsory","SMAP_TOL","1E-4","the tolerance for sketch-map");
  keys.add("compulsory","REGULARISE_PARAM","0.001","this is used to ensure that we don't divide by zero when updating weights");
}

SketchMap::SketchMap( const ActionOptions& ao ):
Action(ao),
DimensionalityReductionBase(ao)
{
  // Read in the switching functions
  std::string linput,hinput, errors;
  parse("HIGH_DIM_FUNCTION",hinput);
  highdf.set(hinput,errors);
  if(errors.length()>0) error(errors);
  parse("LOW_DIM_FUNCTION",linput);
  lowdf.set(hinput,errors);
  if(errors.length()>0) error(errors);

  // Read tolerances
  parse("SMACOF_TOL",smactol);
  parse("SMAP_TOL",smaptol);
  parse("REGULARISE_PARAM",regulariser);
}

void SketchMap::calculateAllDistances( PointWiseMapping* mymap, Matrix<double>& targets ){
  // Calculate matrix of dissimilarities (High dimensional space) 
  mymap->calculateAllDistances( getPbc(), getArguments(), comm, mymap->modifyDmat(), false );
  double dr; unsigned M = mymap->getNumberOfReferenceFrames(); 
  for(unsigned i=1; i<M; ++i){
      for(unsigned j=0; j<i; ++j) {
          targets(i,j) = targets(j,i) = 1.0 - highdf.calculate( mymap->modifyDmat()(i,j), dr ); // high dim space
      }
  }
}


void SketchMap::generateProjections( PointWiseMapping* mymap ){
  Matrix<double> Distances( mymap->modifyDmat() ); //making a copy
  // Calculates the first guess of projections in LD space 
  ClassicalScaling::run( mymap );
  
  // Calculate the value of sigma and the weights
  unsigned M = mymap->getNumberOfReferenceFrames();
  Matrix<double> Weights(M,M); double filt = recalculateWeights( Distances, getTargets(), mymap, Weights );

  unsigned MAXSTEPS=100; double newsig;
  for(unsigned i=0;i<MAXSTEPS;++i){
      // Run the smacof algorithm
      SMACOF::run( Weights, mymap, smactol );
      // Recalculate weights matrix and sigma
      newsig = recalculateWeights( Distances, getTargets(), mymap, Weights );
      printf("HELLO GARETH AND RACHEL %d %f %f %f \n",i, newsig, filt, fabs( newsig - filt ) );
      // Test whether or not the algorithm has converged
      if( fabs( newsig - filt )<smaptol ) break;
      // Make initial sigma into new sigma so that the value of new sigma is used every time so that the error can be reduced
      filt=newsig;
  }
 
  Matrix<double> targets( mymap->modifyDmat() );
  targets = getTargets();
  
  std::vector<double> ld_error(M);
  std::vector<double> cg_error(M);
  std::ofstream myfile;
  double totalerror=0.0;
  
  //Calculates error just after Smacof
  for(unsigned i=0;i<M;i++){
	   std::vector<double> pi(mymap->getNumberOfProperties());
	   std::vector<double> deri(mymap->getNumberOfProperties());
	   //Modify fframes for calculateStress routine	   
	   setTargetVectorForPointwiseGlobalMinimisation(i,targets);
	   for(unsigned k=0;k<mymap->getNumberOfProperties();k++) pi[k] = mymap->getProjectionCoordinate(i,k);
	   double error = calculateStress(pi,deri);
	   ld_error[i] = error;
	   totalerror+=error;
  }


/* Routine to write Projected coordinates after Smacof */
 myfile.open("Smacof.txt");
    for(unsigned i=0;i<M;i++){
		for(unsigned j=0;j<mymap->getNumberOfProperties();j++){
			myfile<<mymap->getProjectionCoordinate(i,j)<<" ";
		}
	myfile<<"\n";
  }
  myfile.close();
  std::cout<<"total error after Smacof "<< totalerror/(M*(M-1)) <<"\n";	
/////////////////////////////////////////////////////////////////////////////   
 
 
   double cgtol = 1E-4;
      for(unsigned i=0;i<M;i++){
	   std::vector<double> p(mymap->getNumberOfProperties());
	   std::vector<double> der(mymap->getNumberOfProperties());
	   for(unsigned j=0;j<mymap->getNumberOfProperties();j++) p[j] = mymap->getProjectionCoordinate(i,j);
       ConjugateGradient<DimensionalityReductionBase> myminimiser2( this );
       myminimiser2.minimise( cgtol, p, &DimensionalityReductionBase::calculateStress );
	  }
  totalerror = 0.0;
 //Calculates error just after SMACOF + CG
  for(unsigned i=0;i<M;i++){
	   std::vector<double> pi(mymap->getNumberOfProperties());
	   std::vector<double> deri(mymap->getNumberOfProperties());
	   //Modify fframes for calculateStress routine	   
	   setTargetVectorForPointwiseGlobalMinimisation(i,targets);
	   for(unsigned k=0;k<mymap->getNumberOfProperties();k++) pi[k] = mymap->getProjectionCoordinate(i,k);
	   double error = calculateStress(pi,deri);
	   ld_error[i] = error;
	   totalerror+=error;
  }
  std::cout<<"total error after Smacof+CG "<< totalerror/(M*(M-1)) <<"\n";

 myfile.open("projections after cg.txt");
  myfile<<"# x y Error \n";
  for(unsigned i=0;i<M;i++){
	 for(unsigned j=0;j<mymap->getNumberOfProperties();j++){
		myfile<<mymap->getProjectionCoordinate(i,j)<<" ";
	 }
	myfile<<ld_error[i];
	myfile<<"\n";
  }
  myfile.close();

//Routine for grid search.
   
   for(int cnt=0;cnt<1;cnt++){
		double minx,miny,maxx,maxy;
		maxy = miny = mymap->getProjectionCoordinate(0,1);
		maxx = minx = mymap->getProjectionCoordinate(0,0);
	  
		for(unsigned i=1;i<mymap->getNumberOfReferenceFrames();i++){
			if(mymap->getProjectionCoordinate(i,0) < minx) minx = mymap->getProjectionCoordinate(i,0);
			if(mymap->getProjectionCoordinate(i,1) < miny) miny = mymap->getProjectionCoordinate(i,1);
			if(mymap->getProjectionCoordinate(i,0) > maxx) maxx = mymap->getProjectionCoordinate(i,0);
			if(mymap->getProjectionCoordinate(i,1) > maxy) maxy = mymap->getProjectionCoordinate(i,1);
		} 
		
       //std::cout<<"minx,maxx, miny,maxy= "<<minx<<" , "<<maxx<<" , " <<miny <<" , "<<maxy<<"\n" ;
	   //Run Grid Search for each frame and get the correct projection.
	   for(unsigned i=0;i<M;i++){
		   setTargetVectorForPointwiseGlobalMinimisation(i,targets);
		   std::vector<double> p(mymap->getNumberOfProperties());
		   std::vector<double> der(mymap->getNumberOfProperties());
		   for(unsigned j=0;j<mymap->getNumberOfProperties();j++) p[j] = mymap->getProjectionCoordinate(i,j);
		   double stepx = fabs(maxx-minx)/100.0;
		   double stepy = fabs(maxy-miny)/100.0;
		   //std::cout<<"stepx,stepy= "<<stepx<<" , "<<stepy<<"\n" ; 
		   std::vector<double> temp( p.size() );
		   std::vector<double> grid_point( p.size() );
		   double min_eng,eng_pt;
		   std::vector< std::vector<double> > stressgrid(101,std::vector<double>(101));
		   double curr_x = minx;
		   double curr_y = miny;
		   grid_point[0] = curr_x;grid_point[1] = curr_y;
		   min_eng = calculateStress(p,der) ;
		   for(int l=-10;l<111;l++){
				curr_x = minx + stepx*l;
				for(int j=-10;j<111;j++){
					curr_y = miny + stepy*j;
//					if(l==0) ptsiny.push_back(curr_y);
					  grid_point[0] = curr_x;grid_point[1] = curr_y;
					  eng_pt = calculateStress(grid_point,der);
//					  stressgrid[l][j] = eng_pt;
                      //std::cout<<"new eng old eng "<<eng_pt<<" "<< min_eng<<"\n";
					  if(eng_pt < min_eng){
						//std::cout<<"found new energy\n";
//						std::cout<<"new eng old eng "<<eng_pt<<" "<< min_eng<<"\r";
						min_eng = eng_pt;			
						p[0] = curr_x;p[1] = curr_y;
						
					  }		  		  
				}
			}
		   
		   //GridSearch<DimensionalityReductionBase> myminimiser( this );
		   //myminimiser.minimise(p,&DimensionalityReductionBase::calculateStress,minx,miny,maxx,maxy);
		    ld_error[i] = calculateStress(p,der);
		   	std::cout<<p[0]<<" "<<p[1]<<" ";
		   
		    temp[0] = p[0];
		    temp[1] = p[1];
		   	ConjugateGradient<DimensionalityReductionBase> myminimiser2( this );
			myminimiser2.minimise( cgtol, temp, &DimensionalityReductionBase::calculateStress );
		   
		   
		    std::cout<<p[0]<<" "<<p[1]<<"\n";
		   for(unsigned j=0;j<p.size();++j) mymap->setProjectionCoordinate(i,j,p[j]);
		   double error = calculateStress(p,der);
		   cg_error[i] = error; 
	   }
	    myfile.open("projections after gridsearch.txt");
		myfile<<"# x y Error before cg after cg""\n";
		for(unsigned i=0;i<M;i++){
			for(unsigned j=0;j<mymap->getNumberOfProperties();j++){
			myfile<<mymap->getProjectionCoordinate(i,j)<<" ";
			}
			myfile<<ld_error[i]<<"  "<<cg_error[i];
			myfile<<"\n";
		}
		myfile.close();
		  
		  // std::cout<<"Projection after GS is "<<p[0]<<" "<<p[1]<<"\n";
		   //Call Conjugate Gradient on it
		   //~ for(int j=0;j<M;j++){
			    //~ if(j!=i){
					//~ setTargetVectorForPointwiseGlobalMinimisation(i,targets);
					//~ for(unsigned k=0;k<mymap->getNumberOfProperties();k++) p[k] = mymap->getProjectionCoordinate(j,k);
					//~ ConjugateGradient<DimensionalityReductionBase> myminimiser2( this );
					//~ myminimiser2.minimise( cgtol, p, &DimensionalityReductionBase::calculateStress );
					//~ for(unsigned t=0;t<p.size();++t) mymap->setProjectionCoordinate( j, t, p[t] ); 
				//~ }
		   //~ }
		  // std::cout<<"Projection after CG is "<<p[0]<<" "<<p[1]<<"\n";
		  // And finally copy the coordinates that you found to the map object.
		  // std::cout<<"Point after conjugate search"<<p[0]<<" "<<p[1]<<"\n";
		  
		  
	   
	  totalerror = 0.0;
	  for(unsigned q=0;q<M;q++){
		   std::vector<double> p(mymap->getNumberOfProperties());
		   std::vector<double> deri(mymap->getNumberOfProperties());	   
		   setTargetVectorForPointwiseGlobalMinimisation(q,targets);
		   for(unsigned k=0;k<mymap->getNumberOfProperties();k++) p[k] = mymap->getProjectionCoordinate(q,k);
		   double error = calculateStress(p,deri);
		   ld_error[q] = error;
		   totalerror+=error;
	  }
	 
	 std::cout<<"total error "<< totalerror/(M*(M-1)) <<"\n";
	 totalerror = 0.0;	
		 
		std::ostringstream fn;
		fn << "filetarget" << cnt << ".txt";
		// Open and write to the file
		std::ofstream out(fn.str().c_str(),std::ios_base::binary);
		for(unsigned i=0;i<M;i++){
			for(unsigned j=0;j<mymap->getNumberOfProperties();j++){
				out<<mymap->getProjectionCoordinate(i,j)<<" ";
			}
			out<<ld_error[i];
			out<<"\n";
		}
		out.close();
   
   }
		  //~ 


}

double SketchMap::recalculateWeights( const Matrix<double>& Distances, const Matrix<double>& F, PointWiseMapping* mymap, Matrix<double>& Weights ){
  double filt=0, totalWeight=0.;; double dr;
  for(unsigned i=1; i<Weights.nrows(); ++i){
      for(unsigned j=0; j<i; ++j){
          double tempd=0;
          for(unsigned k=0;k<mymap->getNumberOfProperties();++k){
             double tmp = mymap->getProjectionCoordinate( i, k ) - mymap->getProjectionCoordinate( j, k );
             tempd += tmp*tmp;
          }
          double ninj=mymap->getWeight(i)*mymap->getWeight(j);
          totalWeight += ninj;

          double dij=sqrt(tempd);
          double fij = 1.0 - lowdf.calculate( dij, dr );
          double filter=F(i,j)-fij;
          double diff=Distances(i,j) - dij;
          if( fabs(diff)<regulariser ) Weights(i,j)=Weights(j,i)=0.0;
          else Weights(i,j)=Weights(j,i) = ( -ninj*filter*dij*dr ) / diff;
          filt += ninj*filter*filter;
      }
  }
  return filt / totalWeight;
}

double SketchMap::transformHD( const double& val, double& df ) const { 
  double vv=1.0 - highdf.calculate( val, df );
  df=-df; return vv;
}
double SketchMap::transformLD( const double& val, double& df ) const {
  double vv=1.0 - lowdf.calculate( val, df );
  df=-df; return vv;
}

}
}
