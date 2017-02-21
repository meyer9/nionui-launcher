if [[ "$TRAVIS_OS_NAME" == "osx" ]] && [[ "$BUILD_SELF" == "1" ]]; then
  brew install qt5 python3
  pip3 install numpy
  sed -i '' 's/QTDIR = $(QTDIR_$(QT_VERSION))/QTDIR = \/usr\/local\/opt\/qt5/g' xcconfig/targetReleaseNionUILauncher.xcconfig
  sed -i '' 's/PYTHONHOME = $(HOME)\/Developer\/anaconda/PYTHONHOME = \/usr\/local\/opt\/python3\/Frameworks\/Python.framework\/Versions\/3.6/g' xcconfig/targetReleaseNionUILauncher.xcconfig
  sed -i '' 's/PYTHON_VERSION_NUMBER = 3.5/PYTHON_VERION_NUMBER = 3.6/g' xcconfig/targetReleaseNionUILauncher.xcconfig
  xcodebuild -project NionUILauncher.xcodeproj -target "Nion UI Launcher" -configuration Release
  cd build/Release
  zip -r NionUILauncher.zip Nion\ UI\ Launcher.app
  cd ../..
fi

if [[ "$TRAVIS_OS_NAME" == "linux" ]] && [[ "$BUILD_SELF" == "1" ]]; then
  wget https://repo.continuum.io/miniconda/Miniconda3-latest-Linux-x86_64.sh
  bash Miniconda3-latest-Linux-x86_64.sh -b -p $HOME/miniconda
  export PATH="$HOME/miniconda/bin:$PATH"
  hash -r
  conda install numpy
  conda info -a
  bash linux_build.sh ~/miniconda
fi

if [[ "$TRAVIS_OS_NAME" == "linux" ]] && [[ "$BUILD_SELF" == "0" ]]; then
  echo "Build windows!"
fi
