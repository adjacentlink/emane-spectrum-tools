/*
 * Copyright (c) 2019 - Adjacent Link LLC, Bridgewater, New Jersey
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Adjacent Link LLC nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "monitorconfigurationfile.h"
#include <emane/utils/parameterconvert.h>

#include <libxml/parser.h>
#include <libxml/xmlschemas.h>
#include <libxml/xpath.h>

#include <cstring>
#include <iostream>

namespace
{
  const char * pzSchema="\
<?xml version='1.0' encoding='UTF-8' standalone='yes'?>\
<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'>\
  <xs:simpleType name='NEMId'>\
    <xs:restriction base='xs:unsignedShort'>\
      <xs:minInclusive value='1'/>\
      <xs:maxInclusive value='65534'/>\
    </xs:restriction>\
  </xs:simpleType>\
  <xs:element name='emane-spectrum-monitor'>\
    <xs:complexType>\
      <xs:sequence>\
        <xs:element name='emulator' minOccurs='0'>\
          <xs:complexType>\
             <xs:sequence>\
               <xs:element name='param' minOccurs='0' maxOccurs='unbounded'>\
                 <xs:complexType>\
                   <xs:attribute name='name' type='xs:string' use='required'/>\
                   <xs:attribute name='value' type='xs:string' use='required'/>\
                 </xs:complexType>\
               </xs:element>\
             </xs:sequence>\
          </xs:complexType>\
        </xs:element>\
        <xs:element name='physical-layer' minOccurs='0'>\
          <xs:complexType>\
             <xs:sequence>\
               <xs:element name='param' minOccurs='0' maxOccurs='unbounded'>\
                 <xs:complexType>\
                   <xs:attribute name='name' type='xs:string' use='required'/>\
                   <xs:attribute name='value' type='xs:string' use='required'/>\
                 </xs:complexType>\
               </xs:element>\
             </xs:sequence>\
          </xs:complexType>\
        </xs:element>\
      </xs:sequence>\
      <xs:attribute name='id' type='NEMId' use='required'/>\
    </xs:complexType>\
  </xs:element>\
</xs:schema>";
}

EMANE::SpectrumTools::MonitorConfigurationFile::MonitorConfigurationFile():
  id_{}{}

void
EMANE::SpectrumTools::MonitorConfigurationFile::load(const std::string & sConfigurationFile)
{
  LIBXML_TEST_VERSION;

  xmlDocPtr pSchemaDoc{xmlReadMemory(pzSchema,
                                     strlen(pzSchema),
                                     "file:///emane-spectrum-monitor.xsd",
                                     NULL,
                                     0)};

  if(!pSchemaDoc)
    {
      throw std::runtime_error{"unable to open schema"};
    }

  xmlSchemaParserCtxtPtr pParserContext{xmlSchemaNewDocParserCtxt(pSchemaDoc)};

  if(!pParserContext)
    {
      throw std::runtime_error{"bad schema context"};
    }

  xmlSchemaPtr pSchema{xmlSchemaParse(pParserContext)};

  if(!pSchema)
    {
      throw std::runtime_error{"bad schema parser"};
    }

  xmlSchemaValidCtxtPtr pSchemaValidCtxtPtr{xmlSchemaNewValidCtxt(pSchema)};

  if(!pSchemaValidCtxtPtr)
    {
      throw std::runtime_error{"bad schema valid context"};
    }

  xmlSchemaSetValidOptions(pSchemaValidCtxtPtr,XML_SCHEMA_VAL_VC_I_CREATE);

  xmlDocPtr pDoc = xmlReadFile(sConfigurationFile.c_str(),nullptr,0);

  if(xmlSchemaValidateDoc(pSchemaValidCtxtPtr, pDoc))
    {
      throw std::runtime_error{"invalid document"};
    }

  xmlXPathContextPtr pXPathCtxt{xmlXPathNewContext(pDoc)};

  xmlNodePtr pRoot = xmlDocGetRootElement(pDoc);

  xmlChar * pNEMId = xmlGetProp(pRoot,BAD_CAST "id");

  id_ = EMANE::Utils::ParameterConvert{reinterpret_cast<char *>(pNEMId)}.toUINT16(1,65534);

  xmlXPathObjectPtr pXPathObj{xmlXPathEvalExpression(BAD_CAST "/emane-spectrum-monitor/emulator/param",
                                                     pXPathCtxt)};

  xmlFree(pNEMId);

  if(!pXPathObj)
    {
      xmlXPathFreeContext(pXPathCtxt);
      xmlFreeDoc(pDoc);
      throw std::runtime_error{"unable to evaluate xpath: /emane-spectrum-monitor/emulator/param"};
    }

  int iSize = pXPathObj->nodesetval->nodeNr;

  for(int i = 0; i < iSize; ++i)
    {
      xmlChar * pName  = xmlGetProp(pXPathObj->nodesetval->nodeTab[i],BAD_CAST "name");
      xmlChar * pValue  = xmlGetProp(pXPathObj->nodesetval->nodeTab[i],BAD_CAST "value");

      emualtorConfiguration_.insert({reinterpret_cast<char *>(pName),
                                     {reinterpret_cast<char *>(pValue)}});

      xmlFree(pName);
      xmlFree(pValue);
    }

  xmlXPathFreeObject(pXPathObj);

  pXPathObj = xmlXPathEvalExpression(BAD_CAST "/emane-spectrum-monitor/physical-layer/param",
                                     pXPathCtxt);

  if(!pXPathObj)
    {
      xmlXPathFreeContext(pXPathCtxt);
      xmlFreeDoc(pDoc);
      throw std::runtime_error{"unable to evaluate xpath: /emane-spectrum-monitor/physical-layer/param"};
    }

  iSize = pXPathObj->nodesetval->nodeNr;

  for(int i = 0; i < iSize; ++i)
    {
      xmlChar * pName  = xmlGetProp(pXPathObj->nodesetval->nodeTab[i],BAD_CAST "name");
      xmlChar * pValue = xmlGetProp(pXPathObj->nodesetval->nodeTab[i],BAD_CAST "value");

      pysicalLayerConfiguration_.insert({reinterpret_cast<char *>(pName),
                                         {reinterpret_cast<char *>(pValue)}});

      xmlFree(pName);
      xmlFree(pValue);
    }

  xmlXPathFreeObject(pXPathObj);

  xmlXPathFreeContext(pXPathCtxt);

  xmlFreeDoc(pDoc);

  xmlCleanupParser();
}

const EMANE::SpectrumTools::MonitorConfigurationFile::ConfigurationMap &
EMANE::SpectrumTools::MonitorConfigurationFile::emualtorConfiguration() const
{
  return emualtorConfiguration_;
}


const EMANE::SpectrumTools::MonitorConfigurationFile::ConfigurationMap &
EMANE::SpectrumTools::MonitorConfigurationFile::physicalLayerConfiguration() const
{
  return pysicalLayerConfiguration_;
}

EMANE::NEMId EMANE::SpectrumTools::MonitorConfigurationFile::id() const
{
  return id_;
}
