#  Humanoid Path Planner - RBPRM-CORBA module

Copyright 2015 LAAS-CNRS

Author: Steve Tonneau

##Description
hpp-rbprm-corba implements python bindings for hpp-rbprm, and presents a few example files.
Please refer to this [link](https://github.com/stonneau/hpp-rbprm) for information on hpp-rbprm.

##Installation on ubuntu-14.04 64 bit with ros-indigo

To install hpp-rbprm-corba automatically:

  1. Go to https://humanoid-path-planner.github.io/hpp-doc/download.html and choose the rbprm installer.

To install hpp-rbprm-corba manually:

  1. Install HPP-RBPRM and its dependencies
	- see https://github.com/stonneau/hpp-rbprm

  2. Install HPP-AFFORDANCE-CORBA along with its dependencies
  - see https://github.com/anna-seppala/hpp-affordance-corba

  3. Use CMake to install the library. For instance:

			mkdir $HPP_RBPRM_CORBA_DIR/build
			cd $HPP_RBPRM_CORBA_DIR/build
			cd cmake ..	
			make install
	


##Documentation

  Open $DEVEL_DIR/install/share/doc/hpp-rbprm-corba/doxygen-html/index.html in a web brower and you
  will have access to the code documentation. If you are using ipython, the documentation of the methods implemented
  is also directly available in a python console.

##Example

  To see the planner in action, two examples from our TRO paper with HyQ are available. Examples with HRP-2 are also provided,
  though they can only be executed if you have access to HRP-2 model.


  - First of all, retrieve and build the former HyQ model from this address:
	https://cloud.laas.fr/index.php/s/ydRq3DoBz2LYMLS

You shoud unzip it in $DEVEL_DIR/install/share/hyq_description/

  - The planning is decomposed in two phases / scripts. First, a root path is computed (\*_path.py files). Then, the contacts are generated along the computed path (\*_interp.py files). The scripts are located in the folder /scripts/demos.

  - To see the different steps of the process run

    ```$ ./run.sh darpa_hyq.py```

  The script include comments explaining the different calls to the library. You can call the different methods a() ... d() to see the different steps of the planning.
