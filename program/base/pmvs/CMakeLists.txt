ADD_LIBRARY(pmvs_lib
	detectFeatures.h
	detectFeatures.cc
	detector.h
	detector.cc
dog.h
dog.cc
expand.h
expand.cc
filter.h
filter.cc
findMatch.h
findMatch.cc
harris.h
harris.cc
optim.h
optim.cc
option.h
option.cc
patch.h
patch.cc
patchOrganizerS.h
patchOrganizerS.cc
point.h
point.cc
seed.h
seed.cc
tex_kernel.h
tex_kernel.cc
)

IF(PMVS_USE_HIGHWAY)
  TARGET_LINK_LIBRARIES(pmvs_lib PUBLIC hwy::hwy)
  TARGET_COMPILE_DEFINITIONS(pmvs_lib PUBLIC PMVS_USE_HIGHWAY=1)
ENDIF(PMVS_USE_HIGHWAY)
