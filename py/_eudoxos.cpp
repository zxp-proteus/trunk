#include<yade/lib-pyutil/numpy.hpp>

#include<yade/pkg-dem/ConcretePM.hpp>
#include<boost/python.hpp>
#include<yade/extra/boost_python_len.hpp>
#include<yade/pkg-dem/Shop.hpp>

using namespace boost::python;
using namespace std;
#ifdef YADE_LOG4CXX
	log4cxx::LoggerPtr logger=log4cxx::Logger::getLogger("yade.eudoxos");
#endif

#ifdef YADE_CPM_FULL_MODEL_AVAILABLE
	#include"../../brefcom-mm.hh"
#endif

# if 0
Real elasticEnergyDensityInAABB(python::tuple Aabb){
	Vector3r bbMin=tuple2vec(python::extract<python::tuple>(Aabb[0])()), bbMax=tuple2vec(python::extract<python::tuple>(Aabb[1])()); Vector3r box=bbMax-bbMin;
	shared_ptr<Scene> rb=Omega::instance().getScene();
	Real E=0;
	FOREACH(const shared_ptr<Interaction>&i, *rb->interactions){
		if(!i->interactionPhysics) continue;
		shared_ptr<CpmPhys> bc=dynamic_pointer_cast<CpmPhys>(i->interactionPhysics); if(!bc) continue;
		const shared_ptr<Body>& b1=Body::byId(i->getId1(),rb), b2=Body::byId(i->getId2(),rb);
		bool isIn1=isInBB(b1->state->pos,bbMin,bbMax), isIn2=isInBB(b2->state->pos,bbMin,bbMax);
		if(!isIn1 && !isIn2) continue;
		Real weight=1.;
		if((!isIn1 && isIn2) || (isIn1 && !isIn2)){
			//shared_ptr<Body> bIn=isIn1?b1:b2, bOut=isIn2?b2:b1;
			Vector3r vIn=(isIn1?b1:b2)->state->pos, vOut=(isIn2?b1:b2)->state->pos;
			#define _WEIGHT_COMPONENT(axis) if(vOut[axis]<bbMin[axis]) weight=min(weight,abs((vOut[axis]-bbMin[axis])/(vOut[axis]-vIn[axis]))); else if(vOut[axis]>bbMax[axis]) weight=min(weight,abs((vOut[axis]-bbMax[axis])/(vOut[axis]-vIn[axis])));
			_WEIGHT_COMPONENT(0); _WEIGHT_COMPONENT(1); _WEIGHT_COMPONENT(2);
			assert(weight>=0 && weight<=1);
		}
		E+=.5*bc->E*bc->crossSection*pow(bc->epsN,2)+.5*bc->G*bc->crossSection*pow(bc->epsT.Length(),2);
	}
	return E/(box[0]*box[1]*box[2]);
}
#endif

/* yield surface for the CPM model; this is used only to make yield surface plot from python, for debugging */
Real yieldSigmaTMagnitude(Real sigmaN, const shared_ptr<Law2_Dem3DofGeom_CpmPhys_Cpm>& functor){
	#ifdef CPM_YIELD_SIGMA_T_MAGNITUDE
		/* find first suitable interaction */
		Scene* rootBody=Omega::instance().getScene().get();
		shared_ptr<Interaction> I;
		FOREACH(I, *rootBody->interactions){
			if(I->isReal()) break;
		}
		if(!I->isReal()) {LOG_ERROR("No real interaction found, returning NaN!"); return NaN; }
		CpmPhys* BC=dynamic_cast<CpmPhys*>(I->interactionPhysics.get());
		if(!BC) {LOG_ERROR("Interaction physics is not a CpmPhys instance, returning NaN!"); return NaN;}
		const Real &omega(BC->omega); const Real& undamagedCohesion(BC->undamagedCohesion); const Real& tanFrictionAngle(BC->tanFrictionAngle);
		const Real& yieldLogSpeed(functor->yieldLogSpeed);
		const int& yieldSurfType(functor->yieldSurfType);
		const Real& yieldEllipseShift(functor->yieldEllipseShift);
		return CPM_YIELD_SIGMA_T_MAGNITUDE(sigmaN);
	#else
		LOG_FATAL("CPM model not available in this build.");
		throw;
	#endif
}


// copied from _utils.cpp
Vector3r tuple2vec(const python::tuple& t){return Vector3r(extract<double>(t[0])(),extract<double>(t[1])(),extract<double>(t[2])());}

/*! Set velocity of all dynamic particles so that if their motion were unconstrained,
 * axis given by axisPoint and axisDirection would be reached in timeToAxis
 * (or, point at distance subtractDist from axis would be reached).
 *
 * The code is analogous to AxialGravityEngine and is intended to give initial motion
 * to particles subject to axial compaction to speed up the process. */
void velocityTowardsAxis(const Vector3r& axisPoint, const Vector3r& axisDirection, Real timeToAxis, Real subtractDist=0., Real perturbation=0.1){
	FOREACH(const shared_ptr<Body>&b, *(Omega::instance().getScene()->bodies)){
		if(!b->isDynamic) continue;
		const Vector3r& x0=b->state->pos;
		const Vector3r& x1=axisPoint;
		const Vector3r x2=axisPoint+axisDirection;
		Vector3r closestAxisPoint=(x2-x1) * /* t */ (-(x1-x0).Dot(x2-x1))/((x2-x1).SquaredLength());
		Vector3r toAxis=closestAxisPoint-x0;
		if(subtractDist>0) toAxis*=(toAxis.Length()-subtractDist)/toAxis.Length();
		b->state->vel=toAxis/timeToAxis;
		Vector3r ppDiff=perturbation*(1./sqrt(3.))*Vector3r(Mathr::UnitRandom(),Mathr::UnitRandom(),Mathr::UnitRandom())*b->state->vel.Length();
		b->state->vel+=ppDiff;
	}
}
BOOST_PYTHON_FUNCTION_OVERLOADS(velocityTowardsAxis_overloads,velocityTowardsAxis,3,5);

void particleConfinement(){
	CpmStateUpdater csu; csu.update();
}

python::dict testNumpy(){
	Scene* scene=Omega::instance().getScene().get();
	int dim1[]={scene->bodies->size()};
	int dim2[]={scene->bodies->size(),3};
	numpy_boost<Real,1> mass(dim1);
	numpy_boost<Real,2> vel(dim2);
	FOREACH(const shared_ptr<Body>& b, *scene->bodies){
		if(!b) continue;
		mass[b->getId()]=b->state->mass;
		VECTOR3R_TO_NUMPY(vel[b->getId()],b->state->vel);
	}
	python::dict ret;
	ret["mass"]=mass;
	ret["vel"]=vel;
	return ret;
}


BOOST_PYTHON_MODULE(_eudoxos){
	import_array();
	def("velocityTowardsAxis",velocityTowardsAxis,velocityTowardsAxis_overloads(args("axisPoint","axisDirection","timeToAxis","subtractDist","perturbation")));
	def("yieldSigmaTMagnitude",yieldSigmaTMagnitude);
	// def("spiralSphereStresses2d",spiralSphereStresses2d,(python::arg("dH_dTheta"),python::arg("axis")=2));
	def("particleConfinement",particleConfinement);
	def("testNumpy",testNumpy);
}

