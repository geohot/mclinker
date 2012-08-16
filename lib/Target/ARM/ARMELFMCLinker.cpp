//===- ARMELFMCLinker.cpp -------------------------------------------------===//
//
//                     The MCLinker Project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "ARMELFMCLinker.h"

#include <mcld/CodeGen/SectLinkerOption.h>

using namespace mcld;

ARMELFMCLinker::ARMELFMCLinker(SectLinkerOption &pOption,
                               TargetLDBackend &pLDBackend)
  : MCLinker(pOption,
               pLDBackend) {
  MCLDInfo &info = pOption.info();
  // set up target-dependent constraints of attributes
  info.attrFactory().constraint().enableWholeArchive();
  info.attrFactory().constraint().enableAsNeeded();
  info.attrFactory().constraint().setSharedSystem();

  // set up the predefined attributes
  info.attrFactory().predefined().unsetWholeArchive();
  info.attrFactory().predefined().unsetAsNeeded();
  info.attrFactory().predefined().setDynamic();

}

ARMELFMCLinker::~ARMELFMCLinker()
{
}
