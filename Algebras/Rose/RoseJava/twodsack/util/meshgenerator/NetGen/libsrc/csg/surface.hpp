#ifndef FILE_SURFACE
#define FILE_SURFACE

/**************************************************************************/
/* File:   surface.hh                                                     */
/* Author: Joachim Schoeberl                                              */
/* Date:   1. Dez. 95                                                     */
/**************************************************************************/




// class DenseMatrix;
// class Box3dSphere;
class TriangleApproximation;

/**
  Basis class for implicit surface geometry.
  This class is used for generation of surface meshes
  in NETGEN as well as for mesh refinement in FEPP.
  */




class Surface
{
protected:
  /// invert normal vector
  bool inverse;
  /// maximal h in surface
  double maxh;
  /// name of surface
  char * name;
  /// boundary condition nr
  int bcprop;
  ///
  
public:
  Surface ();
  /** @name Tangential plane.
    The tangential plane is used for surface mesh generation.
   */
  
  virtual ~Surface();

protected:
  /** @name Points in the surface defining tangential plane.
    Tangential plane is taken in p1, the local x-axis
    is directed to p2.
    */
  //@{
  ///
  Point<3> p1;
  ///
  Point<3> p2;
  //@}
  /** @name Base-vectos for local coordinate system. */
  //@{
  /// in plane, directed p1->p2
  Vec<3> ex;
  /// in plane
  Vec<3> ey;
  /// outer normal direction
  Vec<3> ez;
  //@}
public:

  void SetName (const char * aname);
  const char * Name () const { return name; }

  //@{
  /**
    Defines tangential plane in ap1.
    The local x-coordinate axis point to the direction of ap2 */
  virtual void DefineTangentialPlane (const Point<3> & ap1, 
				      const Point<3> & ap2);

  /// Transforms 3d point p3d to local coordinates pplane
  virtual void ToPlane (const Point<3> & p3d, Point<2> & pplane, 
			double h, int & zone) const;
  
  /// Transforms point pplane in local coordinates to 3d point
  virtual void FromPlane (const Point<2> & pplane, 
			  Point<3> & p3d, double h) const;
  //@}


  /// Move Point p to closes point in surface
  virtual void Project (Point<3> & p) const;


  virtual int IsIdentic (const Surface & /* s2 */, int & /* inv */, 
			 double /* eps */) const
  { return 0; }
  
  ///
  virtual int PointOnSurface (const Point<3> & p,
			      double eps = 1e-6) const;
  

  /** @name Implicit function.
      Calculate function value and derivatives.
  */
  //@{
  /// Calculate implicit function value in point point
  virtual double CalcFunctionValue (const Point<3> & point) const = 0;

  /**
    Calc gradient of implicit function.
    gradient should be O(1) at surface
    */
  virtual void CalcGradient (const Point<3> & point, Vec<3> & grad) const = 0;

  /**
    Calculate second derivatives of implicit function.
   */
  virtual void CalcHesse (const Point<3> & point, Mat<3> & hesse) const;

  /**
    Returns outer normal vector.
   */
  // virtual void GetNormalVector (const Point<3> & p, Vec<3> & n) const;
  virtual Vec<3> GetNormalVector (const Point<3> & p) const;

  /**
    Upper bound for spectral norm of Hesse-matrix
   */
  virtual double HesseNorm () const = 0;

  /**
    Upper bound for spectral norm of Hesse-matrix in the
    rad - environment of point c.
   */
  virtual double HesseNormLoc (const Point<3> & /* c */, 
			       double /* rad */) const
  { return HesseNorm (); }
  //@}


  ///
  virtual double MaxCurvature () const;
  ///
  virtual double MaxCurvatureLoc (const Point<3> & /* c */ , 
				  double /* rad */) const;

  /** Returns any point in the surface.
    Needed to start surface mesh generation e.g. on sphere */
  virtual Point<3> GetSurfacePoint () const = 0;

  ///
  bool Inverse () const { return inverse; }
  ///
  void SetInverse (bool ainverse) { inverse = ainverse; }
  /// 
  virtual void Print (std::ostream & str) const = 0;
  
  ///
  virtual void Reduce (const BoxSphere<3> & /* box */) { };
  ///
  virtual void UnReduce () { };

  /// set max h in surface
  void SetMaxH (double amaxh) { maxh = amaxh; }
  ///
  double GetMaxH () const { return maxh; }
  ///
  int GetBCProperty () const { return bcprop; }
  ///
  void SetBCProperty (int abc) { bcprop = abc; }

  /** Determine local mesh-size.
      Find 
      \[ h \leq hmax, \]
      such that
      \[ h  \times \kappa (x) \leq c \qquad \mbox{in} B(x, h), \]
      where kappa(x) is the curvature in x. */
  virtual double LocH (const Point<3> & p, double x, 
		       double c, double hmax) const;

  /**
     Gets Approximation by triangles,
     where qual is about the number of triangles per radius
  */
  virtual void GetTriangleApproximation (TriangleApproximation & /* tas */, 
					 const Box<3> & /* boundingbox */, 
					 double /* facets */ ) const { };

#ifdef MYGRAPH  
  ///
  virtual void Plot (const class ROT3D & /* rot */) const { };
#endif
  };


typedef enum { IS_OUTSIDE = 0, IS_INSIDE = 1, DOES_INTERSECT = 2}
INSOLID_TYPE;




class Primitive
{

public:

  Primitive ();

  virtual ~Primitive();

  
  /*
    Check, whether box intersects solid defined by surface.

    return values:
    0 .. box outside solid \\
    1 .. box in solid \\
    2 .. can't decide (allowed, iff box is close to solid)
  */
  virtual INSOLID_TYPE BoxInSolid (const BoxSphere<3> & box) const = 0;
  virtual INSOLID_TYPE PointInSolid (const Point<3> & p,
				     double eps) const = 0;
  virtual INSOLID_TYPE VecInSolid (const Point<3> & p,
				   const Vec<3> & v,
				   double eps) const = 0;

  // checks if lim s->0 lim t->0  p + t(v1 + s v2) in solid
  virtual INSOLID_TYPE VecInSolid2 (const Point<3> & p,
				    const Vec<3> & v1,
				    const Vec<3> & v2,
				    double eps) const;

  
  virtual int GetNSurfaces() const = 0;
  virtual Surface & GetSurface (int i = 0) = 0;
  virtual const Surface & GetSurface (int i = 0) const = 0;

  ARRAY<int> surfaceids;
  ARRAY<int> surfaceactive;

  int GetSurfaceId (int i = 0) const;
  void SetSurfaceId (int i, int id);
  int SurfaceActive (int i) const { return surfaceactive[i]; }
  virtual int SurfaceInverted (int i = 0) const { return 0; }

  virtual void GetPrimitiveData (char *& classname, 
				 ARRAY<double> & coeffs) const;
  virtual void SetPrimitiveData (ARRAY<double> & coeffs);
  static Primitive * CreatePrimitive (const char * classname);


  virtual void Reduce (const BoxSphere<3> & /* box */) { };
  virtual void UnReduce () { };

  virtual Primitive * Copy () const;
  virtual void Transform (Transformation<3> & trans);
};




class OneSurfacePrimitive : public Surface, public Primitive
{
public:
  OneSurfacePrimitive();
  ~OneSurfacePrimitive();

  virtual INSOLID_TYPE PointInSolid (const Point<3> & p,
				     double eps) const;
  virtual INSOLID_TYPE VecInSolid (const Point<3> & p,
				   const Vec<3> & v,
				   double eps) const;
  virtual INSOLID_TYPE VecInSolid2 (const Point<3> & p,
				    const Vec<3> & v1,
				    const Vec<3> & v2,
				    double eps) const;

  
  virtual int GetNSurfaces() const;
  virtual Surface & GetSurface (int i = 0);
  virtual const Surface & GetSurface (int i = 0) const;
};






/**
  Projects point to edge.
  The point hp is projected to the edge descibed by f1 and f2.
  It is assumed that the edge is non-degenerated, and the
  (generalized) Newton method converges.
 */
extern void ProjectToEdge (const Surface * f1, 
			   const Surface * f2,
			   Point<3> & hp);



#endif
