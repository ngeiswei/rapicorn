The Rapicorn Toolkit
====================

[![License MPL2](http://testbit.eu/~timj/pics/license-mpl-2.svg)](https://github.com/tim-janik/rapicorn/blob/master/COPYING.MPL)
[![Build Status](https://travis-ci.org/tim-janik/rapicorn.svg)](https://travis-ci.org/tim-janik/rapicorn)
[![Binary Download](https://api.bintray.com/packages/beast-team/deb/rapicorn/images/download.svg)](https://github.com/tim-janik/rapicorn/#binary-packages)


## DESCRIPTION

Rapicorn is a graphical user interface (UI) toolkit for rapid development
of user interfaces in C++ and Python. The user interface (UI) is designed
in declarative markup language and is connected to the programming logic
using data bindings and commands.

*   For a full description, visit the project website:
	http://rapicorn.org

*   To submit bug reports and feature requests, visit:
	https://github.com/tim-janik/rapicorn/issues

*   Rapicorn is currently in the "prototype" phase. Features are still
	under heavy development. Details are provided in the roadmap:
	http://rapicorn.org/wiki/Rapicorn_Task_List#Roadmap


## REQUIREMENTS

Rapicorn has been successfully build on Ubuntu x86-32 and x86-64.
A number of dependency packages need to be installed:

    apt-get install intltool librsvg2-dev libpango1.0-dev libxml2-dev \
      libreadline6-dev python2.7-dev python-enum34 \
      xvfb cython doxygen graphviz texlive-binaries pandoc

## INSTALLATION

In short, Rapicorn needs to be built and installed with:

	./configure
	make -j`nproc`
	make -j`nproc` check		# run simple unit tests
	make install
	make -j`nproc` installcheck	# run module tests

Note that Rapicorn has to be fully installed to function properly.
For non-standard prefixes, Python module imports need proper search
path setups. The following commands shows two examples:

	make python-call-info -C cython/


## SUPPORT

If you have any issues, please let us know in the issue tracker or
the mailing list / web forum:

	https://groups.google.com/d/forum/rapicorn
	rapicorn@googlegroups.com

The developers can often be found chatting on IRC:

	#beast IRC channel on GimpNet: irc.gimp.org

The distribution tarball includes Python and C++ tests and examples:

	examples/  tests/

Documentation is provided online and locally (if installed in /usr):

*   https://testbit.eu/pub/docs/rapicorn/latest/

*   file:///usr/share/doc/rapicorn/html/index.html


## BINARY PACKAGES

New source code pushed to the Rapicorn repository is automatically built
and tested through a travis-ci script. Successful travis-ci builds also
create binary Debian packages
([latest version](https://bintray.com/beast-team/deb/rapicorn/_latestVersion))
which can be installed after adding an apt data source. Here is an example
for Ubuntu wily, for other distributions substitute 'wily' with the name
of one of our [release builds](https://bintray.com/beast-team/deb/):

    # Enable HTTPS transports for apt
    sudo apt-get -y install apt-transport-https ca-certificates
    # Install the bintray key and add the beast-team packages
    sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 379CE192D401AB61
    echo "deb [trusted=yes] https://dl.bintray.com/beast-team/deb wily main" |
      sudo tee -a /etc/apt/sources.list.d/beast-team.list
    # Update package list and install Rapicorn
    sudo apt-get update && apt-get -y install rapicorn

Please note, that these packages and building from upstream source often
introduces API or ABI incompatibilities that the shared-object library
version may not properly reflect. So rebuilding or manual adjustments
for dependent packages may be needed. The official release tarballs on
the project website do not have this problem.
