// C++ includes
#include <iostream>
#include <string>
#include <math.h>


// Local Includes
#include "elem.h"
#include "fe.h"
#include "libmesh.h"
#include "mesh.h"
#include "perf_log.h"
#include "quadrature_gauss.h"
#include "tecplot_io.h"
#include "tree.h"
#include "legacy_xdr_io.h"



int main (int argc, char** argv)
{
  libMesh::init (argc, argv);

  
  {  
    PerfLog perf_log("main()");
    
    const unsigned int dim = 3;
  
    if (argc < 6)
      {
	std::cerr << "Usage: " << argv[0] 
		  << " ivar m0.mesh m1.mesh s0.soln s1.soln"
		  << std::endl;
	
	error();
      }

    // declare the coarse and fine meshes.
    Mesh mesh_coarse(dim);
    Mesh mesh_fine(dim);

    // Read the coarse mesh
    {
      std::cout << "Reading Mesh " << argv[2] << std::endl;
      mesh_coarse.read(argv[2]);
      mesh_coarse.print_info();
      std::cout << std::endl;
    }
    // Read the fine mesh
    {
      std::cout << "Reading Mesh " << argv[3] << std::endl;
      mesh_fine.read(argv[3]);
      mesh_fine.print_info();
      std::cout << std::endl;
    }


    std::vector<Number> coarse_solution;
    std::vector<Number> fine_solution;
    std::vector<std::string> coarse_var_names;
    std::vector<std::string> fine_var_names;
  
    // Read the coarse solution
    {
      std::cout << "Reading Soln " << argv[4] << std::endl;
      LegacyXdrIO(mesh_coarse,true).read_mgf_soln(std::string(argv[4]),
					          coarse_solution,
					          coarse_var_names);
    }

    // Read the fine solution
    {
      std::cout << "Reading Soln " << argv[5] << std::endl;
      LegacyXdrIO(mesh_fine,true).read_mgf_soln(std::string(argv[5]),
					        fine_solution,
					        fine_var_names);
    }

  
    assert (fine_var_names == coarse_var_names);


    std::vector<Number>      diff_solution  (fine_solution.size());
    std::vector<std::string> diff_var_names (fine_var_names);

    // Declare an Octree.
    perf_log.start_event("octree build");
    Trees::OctTree octree_coarse(mesh_coarse,100);
    perf_log.stop_event("octree build");
  
    std::cout << "n_active_bins() = " << octree_coarse.n_active_bins() << std::endl;

    // sanity check.  Make sure that we can find all the element
    // centroids and all the mesh nodes!
    /*
      for (unsigned int e=0; e<mesh_coarse.n_elem(); e++)
      {
      std::cout << "looking for centroid of element " << e << std::endl;
      const Elem* elem = octree_coarse.find_element(mesh_coarse.elem(e)->centroid(mesh_coarse));

      assert (elem != NULL);
      }
      for (unsigned int n=0; n<mesh_coarse.n_nodes(); n++)
      {
      std::cout << "looking for node " << n << std::endl;
      const Elem* elem = octree_coarse.find_element(mesh_coarse.vertex(n));

      assert (elem != NULL);
      }
    */

    // Here we will do the integration.  This deserves some explanation:
    // 1.) The integration will be done by looping over the fine mesh elements.
    //     The fine solution values will be computed at each Gauss point on the
    //     fine grid.
    // 2.) Then the coarse grid element containing the current Gauss point will
    //     be found.  The value of the coarse solution at the Gauss point will
    //     be interpolated.
    // 3.) The difference betwee the fine and coarse solutions will be integrated.
    {
      // Use super--accurate quadrature to avoid superconvergent points
      //    QGauss qrule (dim, NINTH);
      QGauss qrule (dim, FIFTH);
    
      // Declare second--order elements for our Hex27's
      FiniteElements::FELagrange3D fe_coarse (SECOND);
      FiniteElements::FELagrange3D fe_fine   (SECOND);
    
      fe_coarse.attach_quadrature_rule (&qrule);
      fe_fine.attach_quadrature_rule   (&qrule);
    
      const std::vector<Real>& JxW               = fe_fine.get_JxW();
      const std::vector<Point>& q_point          = fe_fine.get_xyz();
      const std::vector<std::vector<Real> >& phi = fe_fine.get_phi();
      const int ivar = atoi(argv[1]);
      Number error = 0.;

      // Initial coarse element
      Elem* coarse_element = mesh_coarse.elem(0);
      fe_coarse.reinit (coarse_element);

    
      // Loop over fine mesh elements
      for (unsigned int e=0; e<mesh_fine.n_elem(); e++)
	{
	  const Elem* fine_element = mesh_fine.elem(e);

	  // Recompute the element--specific data for the current fine-mesh element.
	  fe_fine.reinit(fine_element);

	  // Loop over the fine element's Gauss Points
	  perf_log.start_event("gp_loop");

	  for (unsigned int gp=0; gp<q_point.size(); gp++)
	    {
	      Number fine_soln=0., coarse_soln=0.;

	      assert (fe_fine.n_shape_functions() == fine_element->n_nodes());
	    
	      for (unsigned int i=0; i<fe_fine.n_shape_functions(); i++)
		{
		  const unsigned int nv = fine_var_names.size();
		  const unsigned int gn = fine_element->node(i); // Global node number
				
		  fine_soln += fine_solution[gn*nv + ivar]*phi[i][gp];
		}


	      // Chances are this Gauss point is contained in the coarse-mesh element that contained
	      // the last Gauss point, so let's look there first and only do the OctTree search
	      // if necessary.
	      if (!coarse_element->contains_point(q_point[gp]))
		{
		  perf_log.pause_event("gp_loop");
		  perf_log.start_event("element lookup");

		  coarse_element = const_cast<Elem*>(octree_coarse.find_element(q_point[gp]));
		
		  assert (coarse_element != NULL);
				
		  // Recompute the element--specific data for the new coarse-mesh element.
		  fe_coarse.reinit (coarse_element);
		
		  perf_log.stop_event("element lookup");
		  perf_log.restart_event("gp_loop");
		}
	    

	      // Find the point on the coarse reference element corresponding to the current Gauss
	      // point
	      const Point mapped_point = fe_coarse.inverse_map(coarse_element, q_point[gp]);

	      // Interpolate the coarse grid solution.
	      for (unsigned int i=0; i<fe_coarse.n_shape_functions(); i++)
		{		
		  const unsigned int nv = coarse_var_names.size();
		  const unsigned int gn = coarse_element->node(i); // Global node number
				
		  coarse_soln += coarse_solution[gn*nv + ivar]*fe_coarse.shape(coarse_element,
									       SECOND,
									       i,
									       mapped_point);
		}

	      // Accumulate the error.
	      error += JxW[gp]*(coarse_soln - fine_soln)*(coarse_soln - fine_soln);
	    }

	  perf_log.stop_event("gp_loop");

	} // End element loop

      error = sqrt(error);

      std::cout << "Computed error=" << error
		<< std::endl;

      // Now lets compute the error at each node in the fine mesh and plot it out.
      {
	perf_log.start_event ("diff_soln_loop");
      
	const unsigned int nv = diff_var_names.size();

	std::vector<unsigned char> already_done(mesh_fine.n_nodes(), 0);

	Elem* coarse_element = mesh_coarse.elem(0);
      
	for (unsigned int e=0; e<mesh_fine.n_elem(); e++)
	  for (unsigned int n=0; n<mesh_fine.elem(e)->n_nodes(); n++)
	    {
	      const unsigned int gn = mesh_fine.elem(e)->node(n);
	    
	      if (!already_done[gn])
		{
		  already_done[gn] = 1;
	      
		  const Point& p = mesh_fine.point(gn);
		
		  if (!coarse_element->contains_point(p))
		    {
		      perf_log.pause_event ("diff_soln_loop");
		      perf_log.start_event ("element lookup 2");
		    
		      coarse_element = const_cast<Elem*>(octree_coarse.find_element(p));
		
		      assert (coarse_element != NULL);
		    
		      // Recompute the element--specific data for the new coarse-mesh element.
		      fe_coarse.reinit (coarse_element);
		    
		      perf_log.stop_event ("element lookup 2");
		      perf_log.restart_event ("diff_soln_loop");
		    }
		
		  const Point mapped_point = fe_coarse.inverse_map(coarse_element, p);
		
		  for (unsigned int c=0; c<nv; c++)
		    {
		      Number coarse_soln = 0.;
		    
		      // Interpolate the coarse grid solution.
		      for (unsigned int i=0; i<fe_coarse.n_shape_functions(); i++)
			coarse_soln += coarse_solution[coarse_element->node(i)*nv + c]*fe_coarse.shape(coarse_element,
												       SECOND,
												       i,
												       mapped_point);
		    
		      diff_solution[gn*nv + c] = coarse_soln - fine_solution[gn*nv + c];
		    }	    
		}
	    }
	perf_log.stop_event ("diff_soln_loop");
      }

      std::string plot_name = "foo.plt";
      TecplotIO(mesh_fine).write_nodal_data(plot_name, diff_solution, diff_var_names);

    }
  }
  
    
  return libMesh::close();
}
  
