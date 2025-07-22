  # Get the directory of the current script
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

  # Go 2 directories up
  WORKSPACE="$(cd "$SCRIPT_DIR/../.." && pwd)"

  # Export the WORKSPACE variable
  export WORKSPACE
  
  # Clone required packages
  rm -rf ${WORKSPACE}/sources
  mkdir -p ${WORKSPACE}/sources

  pushd ${WORKSPACE}/sources

  git clone --depth 1 --shallow-submodules -b 0.24.0 https://github.com/xtensor-stack/xtensor.git
  git clone --depth 1 --shallow-submodules -b 0.20.0 https://github.com/xtensor-stack/xtensor-blas.git
  git clone --depth 1 --shallow-submodules -b 0.7.3 https://github.com/xtensor-stack/xtl.git
  git clone --depth 1 --shallow-submodules -b v3.0.0 https://github.com/jarro2783/cxxopts.git
  git clone --depth 1 --shallow-submodules -b v1.1.0 https://github.com/Tencent/rapidjson.git
  git clone --depth 1 --shallow-submodules -b v2.11.0 https://github.com/pybind/pybind11.git

  mkdir -p ${WORKSPACE}/core/open_source/xtensor_stack/base
  mkdir -p ${WORKSPACE}/core/open_source/xtensor_stack/blas
  mkdir -p ${WORKSPACE}/core/open_source/cxxopts
  mkdir -p ${WORKSPACE}/core/open_source/rapidjson
  mkdir -p ${WORKSPACE}/core/open_source/pybind11

  cp -r xtensor/include/. ${WORKSPACE}/core/open_source/xtensor_stack/base
  cp -r xtensor-blas/include/. ${WORKSPACE}/core/open_source/xtensor_stack/blas
  cp -r xtl/include/. ${WORKSPACE}/core/open_source/xtensor_stack/base
  cp -r cxxopts/include/. ${WORKSPACE}/core/open_source/cxxopts
  cp -r rapidjson/include/. ${WORKSPACE}/core/open_source/rapidjson
  cp -r pybind11/include/. ${WORKSPACE}/core/open_source/pybind11
  popd
  
  # Clone Catch2 required packages
  pushd ${WORKSPACE}/sources
  git clone --depth 1 --shallow-submodules -b v2.13.7 https://github.com/catchorg/Catch2.git
  mkdir -p ${WORKSPACE}/core/open_source/catch2
  cp -r Catch2/single_include/catch2/. ${WORKSPACE}/core/open_source/catch2/
  popd
