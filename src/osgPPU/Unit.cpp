/***************************************************************************
 *   Copyright (c) 2008   Art Tevs                                         *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 3 of        *
 *   the License, or (at your option) any later version.                   *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Lesse General Public License for more details.                    *
 *                                                                         *
 *   The full license is in LICENSE file included with this distribution.  *
 ***************************************************************************/

#include <osgPPU/Unit.h>
#include <osgPPU/Processor.h>
#include <osgPPU/BarrierNode.h>
#include <osgPPU/Utility.h>

#include <osg/Texture2D>
#include <osgDB/WriteFile>
#include <osgDB/Registry>
#include <osg/Image>
#include <osg/Program>
#include <osg/FrameBufferObject>
#include <osg/Geometry>
#include <math.h>

namespace osgPPU
{

//------------------------------------------------------------------------------
Unit::Unit()
{
    initialize();
}

//------------------------------------------------------------------------------
void Unit::initialize()
{
    setName("__Nameless_PPU_");
    mUserData = NULL;
    mInputTexIndexForViewportReference = 0;
    setIndex(-1);

    // we do steup defaults
    setActive(true);
    setOfflineMode(false);
    mOutputInternalFormat = GL_RGBA16F_ARB;
    mbTraversed = false;
    mbTraversedMask = false;

    // create default geode
    mGeode = new osg::Geode();
    mGeode->setCullingActive(false);
    addChild(mGeode.get());

    // add empty mrt=0 output texture 
    setOutputTexture(NULL, 0);
            
    // initialze projection matrix
    sProjectionMatrix = new osg::RefMatrix(osg::Matrix::ortho(0,1,0,1,0,1));
    
    // setup default modelview matrix
    sModelviewMatrix = new osg::RefMatrix(osg::Matrixf::identity());

    // dirty anything
    dirty();

    // setup default empty fbo and empty program, so that in default mode
    // we do not use any fbo or program
    getOrCreateStateSet()->setAttribute(new osg::Program(), osg::StateAttribute::ON);
    getOrCreateStateSet()->setAttribute(new osg::FrameBufferObject(), osg::StateAttribute::ON);

    // we also setup empty textures so that this unit do not get any input texture 
    // as long as one is not defined
    for (unsigned int i=0; i < 16; i++)
    {
        getOrCreateStateSet()->setTextureAttribute(i, new osg::Texture2D());
    }

    // no culling, because we do not need it
    setNumChildrenRequiringUpdateTraversal(1);
    setCullingActive(false);
}

//------------------------------------------------------------------------------
Unit::Unit(const Unit& ppu, const osg::CopyOp& copyop) :
    osg::Group(ppu, copyop),
    mInputTex(ppu.mInputTex),
    mOutputTex(ppu.mOutputTex),
    mIgnoreList(ppu.mIgnoreList),
    mInputToUniformMap(ppu.mInputToUniformMap),
    mShader(ppu.mShader),
    mIndex(ppu.mIndex),
    mDrawable(ppu.mDrawable),
    sProjectionMatrix(ppu.sProjectionMatrix),
    sModelviewMatrix(ppu.sModelviewMatrix),
    mViewport(ppu.mViewport),
    mbDirty(ppu.mbDirty),
    mbOfflinePPU(ppu.mbOfflinePPU),
    mOutputInternalFormat(ppu.mOutputInternalFormat),
    mInputTexIndexForViewportReference(ppu.mInputTexIndexForViewportReference),
    mbActive(ppu.mbActive),
    mbTraversed(ppu.mbTraversed),
    mbTraversedMask(ppu.mbTraversedMask),
    mUserData(ppu.mUserData)
{

}

//------------------------------------------------------------------------------
Unit::~Unit()
{
}

//------------------------------------------------------------------------------
osg::Drawable* Unit::createTexturedQuadDrawable(const osg::Vec3& corner,const osg::Vec3& widthVec,const osg::Vec3& heightVec, float l, float b, float r, float t)
{
    osg::Geometry* geom = new osg::Geometry();

    osg::Vec3Array* coords = new osg::Vec3Array(4);
    (*coords)[0] = corner+heightVec;
    (*coords)[1] = corner;
    (*coords)[2] = corner+widthVec;
    (*coords)[3] = corner+widthVec+heightVec;
    geom->setVertexArray(coords);

    osg::Vec2Array* tcoords = new osg::Vec2Array(4);
    (*tcoords)[0].set(l,t);
    (*tcoords)[1].set(l,b);
    (*tcoords)[2].set(r,b);
    (*tcoords)[3].set(r,t);
    geom->setTexCoordArray(0,tcoords);

    osg::Vec4Array* colours = new osg::Vec4Array(1);
    (*colours)[0].set(1.0f,1.0f,1.0,1.0f);
    geom->setColorArray(colours);
    geom->setColorBinding(osg::Geometry::BIND_OVERALL);

    osg::Vec3Array* normals = new osg::Vec3Array(1);
    (*normals)[0] = widthVec^heightVec;
    (*normals)[0].normalize();
    geom->setNormalArray(normals);
    geom->setNormalBinding(osg::Geometry::BIND_OVERALL);

    geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS,0,4));

    // remove colors form geometry
    osg::Vec4Array* screenQuadColor = new osg::Vec4Array(1);
    (*screenQuadColor)[0].set(1.0f,1.0f,1.0,1.0f);
    geom->setColorArray(screenQuadColor);
    geom->setColorBinding(osg::Geometry::BIND_OFF);
    geom->setStateSet(new osg::StateSet());
    geom->setUseDisplayList(false);

    // setup draw callback for it
    geom->setDrawCallback(new Unit::DrawCallback(this));
   
    return geom;
}

//------------------------------------------------------------------------------
void Unit::setRenderingFrustum(float left, float top, float right, float bottom)
{
    sProjectionMatrix = new osg::RefMatrix(osg::Matrix::ortho2D(left, right, bottom, top));
}

//------------------------------------------------------------------------------
void Unit::setInputTextureIndexForViewportReference(int index)
{
    if (index != mInputTexIndexForViewportReference)
    {
        if (index < 0)
            mInputTexIndexForViewportReference = -1;
        else
            mInputTexIndexForViewportReference = index;

        dirty();
    }
}


//------------------------------------------------------------------------------
void Unit::setOutputTexture(osg::Texture* outTex, int mrt)
{
    if (outTex)
        mOutputTex[mrt] = outTex;
    else
        mOutputTex[mrt] = osg::ref_ptr<osg::Texture>(NULL);

    dirty();
}

//------------------------------------------------------------------------------
bool Unit::setInputToUniform(Unit* parent, const std::string& uniform, bool add)
{
    if (parent == NULL || uniform.length() < 1) return false;

    // add this uniform as a child of the parent if required
    if (add && !parent->containsNode(this)) parent->addChild(this);

    // check if this is a valid parent of this node
    unsigned int index = getNumParents();
    for (unsigned int i=0; i < getNumParents(); i++)
        if (getParent(i) == parent)
        {
            index = i;
            break;
        }

    if (index == getNumParents()) return false;
    
    // add the uniform
    mInputToUniformMap[parent] = std::pair<std::string, unsigned int>(uniform, index);

    dirty();

    return true;
}

//--------------------------------------------------------------------------
void Unit::removeInputToUniform(const std::string& uniform, bool del)
{
    // search for this uniform
    InputToUniformMap::iterator it = mInputToUniformMap.begin();
    for (; it != mInputToUniformMap.end(); it++)
    {
        if (it->second.first == uniform)
        {
            // remove from the stateset
            mGeode->getOrCreateStateSet()->removeUniform(uniform);

            // if we have to remove the parent
            if (del) it->first->removeChild(this);

            // and finally remove the element from the list
            mInputToUniformMap.erase(it);

            dirty();
            break;
        }
    }    
}
        
//--------------------------------------------------------------------------
void Unit::removeInputToUniform(Unit* parent, bool del)
{
    // search for this uniform
    InputToUniformMap::iterator it = mInputToUniformMap.begin();
    for (; it != mInputToUniformMap.end(); it++)
        if (it->first.get() == parent)
        {
            removeInputToUniform(it->second.first, del);
            break;
        }
}

//--------------------------------------------------------------------------
void Unit::assignInputTexture()
{
    // here the textures will be applied
    osg::StateSet* ss = getOrCreateStateSet();

    // for all entries
    TextureMap::iterator it = mInputTex.begin();
    for (; it != mInputTex.end(); it++)
    {
        // set texture if it is valid
        if (it->second.valid())
        {
            ss->setTextureAttributeAndModes(it->first, it->second.get(), osg::StateAttribute::ON);
        }
    }
}

//--------------------------------------------------------------------------
void Unit::assignShader()
{
    osg::StateSet* ss = getOrCreateStateSet();
    
    // set shader if it is valid
    if (mShader.valid())
    {
        // enable shader 
        mShader->enable(ss);
        ss->setAttributeAndModes(mShader->getProgram(), osg::StateAttribute::ON);

        // notice about changes in the shader assignment
        noticeAssignShader();
    }
}

//--------------------------------------------------------------------------
void Unit::removeShader()
{
    osg::StateSet* ss = getOrCreateStateSet();
    
    // set shader if it is valid
    if (mShader.valid())
    {
        mShader->disable(ss);
        noticeRemoveShader();
    }
}

//--------------------------------------------------------------------------
void Unit::setViewport(osg::Viewport* vp)
{
    // if viewport is valid and we have to ignore new settings
    if (vp == NULL) return;

    // otherwise setup new viewport
    mViewport = new osg::Viewport(*vp);
    assignViewport();

    dirty();
}

//--------------------------------------------------------------------------
void Unit::assignViewport()
{
    if (mViewport.valid())
    {
        getOrCreateStateSet()->setAttribute(mViewport.get(), osg::StateAttribute::ON);
    }
}

//--------------------------------------------------------------------------
void Unit::setOutputInternalFormat(GLenum format)
{
    mOutputInternalFormat = format;
    
    // now generate output texture's and assign them to fbo 
    TextureMap::iterator it = mOutputTex.begin();
    for (;it != mOutputTex.end(); it++)
    {
        if (it->second.valid()){
            it->second->setInternalFormat(mOutputInternalFormat);
            it->second->setSourceFormat(createSourceTextureFormat(mOutputInternalFormat));
        }
    }

}

//--------------------------------------------------------------------------
void Unit::setIgnoreInput(unsigned int index, bool ignore)
{
    bool ignored = getIgnoreInput(index);
    
    // if want to ignore and not ignored before, hence do it
    if (ignore && !ignored) 
    {
        mIgnoreList.push_back(index);
        dirty();
    }

    // if ignored and want to not ignore anymore
    else if (!ignore && ignored)
    {
        // remove the fix mark
        for (IgnoreInputList::iterator it = mIgnoreList.begin(); it != mIgnoreList.end();)
            if (*it == index) it = mIgnoreList.erase(it);
            else it++;

        dirty();
    }   
}

//--------------------------------------------------------------------------
bool Unit::getIgnoreInput(unsigned int index) const
{
    for (IgnoreInputList::const_iterator it = mIgnoreList.begin(); it != mIgnoreList.end(); it++)
        if (*it == index) return true;
    return false;
}

//--------------------------------------------------------------------------
void Unit::updateUniforms()
{
    // we do get the stateset of the geode, so that we do not 
    // get problems with the shader specified on the unit' stateset
    osg::StateSet* ss = mGeode->getOrCreateStateSet();

    // viewport specific uniforms
    if (mViewport.valid())
    {
        osg::Uniform* w = ss->getOrCreateUniform(OSGPPU_VIEWPORT_WIDTH_UNIFORM, osg::Uniform::FLOAT);
        osg::Uniform* h = ss->getOrCreateUniform(OSGPPU_VIEWPORT_HEIGHT_UNIFORM, osg::Uniform::FLOAT);
        w->set((float)mViewport->width());
        h->set((float)mViewport->height());
    }

    // setup input texture uniforms
    InputToUniformMap::iterator it = mInputToUniformMap.begin();
    for (; it != mInputToUniformMap.end(); it++)
    {
        // only valid inputs
        if (it->first.valid())
        {
            // setup uniform
            osg::Uniform* tex = ss->getOrCreateUniform(it->second.first,
                convertTextureToUniformType(it->first->getOutputTexture(0)));
            tex->set((int)it->second.second);
        }
    }
    
}

//--------------------------------------------------------------------------
void Unit::update()
{
    if (mbDirty)
    {
        init();
        printDebugInfo();
        mbDirty = false;
        updateUniforms();
    }
}

//------------------------------------------------------------------------------
void Unit::traverse(osg::NodeVisitor& nv)
{
    // for the special case that we have culling or update traversion
    if (nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR
        || nv.getVisitorType() == osg::NodeVisitor::UPDATE_VISITOR)
    {
        // perform traversion only if mask valid to the flag
        // this would give us a depth-first-traversion
        if (mbTraversed == mbTraversedMask)
        {
            mbTraversed = !mbTraversedMask;
    
            osg::Group::traverse(nv);
        }

    // for all other cases do just traverse
    }else 
        osg::Group::traverse(nv);
}

//------------------------------------------------------------------------------
void Unit::init()
{
    // collect all inputs from the units above
    setupInputsFromParents();

    // check if we have input reference size 
    if (getInputTextureIndexForViewportReference() >= 0 && getInputTexture(getInputTextureIndexForViewportReference()))
    {
        // if no viewport, so create it
        if (!mViewport.valid())
            mViewport = new osg::Viewport(0,0,0,0);
        
        // change viewport sizes
        mViewport->width() = getInputTexture(getInputTextureIndexForViewportReference())->getTextureWidth();
        mViewport->height() = getInputTexture(getInputTextureIndexForViewportReference())->getTextureHeight();

        // just notice that the viewport size is changed
        noticeChangeViewport();
    }

    // reassign input and shaders
    assignInputTexture();
    assignShader();
    assignViewport();
}


//--------------------------------------------------------------------------
// Helper class for collecting inputs from unit parents
//--------------------------------------------------------------------------
class CollectInputParents : public osg::NodeVisitor
{
public:
    CollectInputParents(Unit* caller) : 
         osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_PARENTS),
        _caller(caller)
    {
        _inputUnitsFound = false;
    }

    void apply(osg::Group& node)
    {
        Unit* unit = dynamic_cast<osgPPU::Unit*>(&node);
        Processor* proc = dynamic_cast<osgPPU::Processor*>(&node);

        // check if the traversed node is an unit
        if (unit != NULL && unit != _caller)
        {
            // first force the unit to recompile its outputs 
            // the update method should do this if it wasn't done before
            unit->update();

            // get the output texture 0 as input
            _input.push_back(unit->getOrCreateOutputTexture(0));

            _inputUnitsFound = true;
            
        // if it is a processor, then get the camera attachments as inputs
        }else if (proc != NULL)
        {
            // get first color attachment from the camera
            osg::Camera::BufferAttachmentMap& map = proc->getCamera()->getBufferAttachmentMap();
            osg::Texture* input = map[osg::Camera::COLOR_BUFFER]._texture.get();
            
            _input.push_back(input);

        // nothing else, then just traverse
        }else
            traverse(node);
    }

    Unit* _caller;
    std::vector<osg::Texture*> _input;
    bool _inputUnitsFound;
};
    
    
//--------------------------------------------------------------------------
// Helper class to find the processor
//--------------------------------------------------------------------------
class FindProcessor: public osg::NodeVisitor
{
public:
    FindProcessor() : osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_PARENTS)
    {
        _processor = NULL;
    }

    void apply(osg::Group& node)
    {
        _processor = dynamic_cast<osgPPU::Processor*>(&node);        
        if (_processor == NULL) traverse(node);
    }

    osgPPU::Processor* _processor;
};


//--------------------------------------------------------------------------
void Unit::setupInputsFromParents()
{
    // if the ppu isoffline, then do nothing
    if (getOfflineMode()) return;

    // use a visitor to collect all inputs from parents
    CollectInputParents cp(this);
    this->accept(cp);

    // add each found texture as input to the unit
    bool changedInput = false;
    for (unsigned int i=0, k=0; k < cp._input.size(); k++)
    {
        // add as input texture
        if (!getIgnoreInput(k))
        {
            mInputTex[i++] = cp._input[k];
            changedInput = true;
        }
    }
    if (changedInput) noticeChangeInput();

    // if viewport is not defined and we need viewport from processor, then 
    if (getViewport() == NULL &&
        (getInputTextureIndexForViewportReference() < 0
        || getInputTextureIndexForViewportReference() >=0 && !cp._inputUnitsFound))
    {
        // find the processor
        FindProcessor fp;
        this->accept(fp);

        // check if processor found
        if (fp._processor == NULL)
        {
            osg::notify(osg::FATAL) << "osgPPU::Unit::setupInputsFromParents() - " << getName() <<" - is not able to find the unit processor!" << std::endl;
            return;
        }

        // get viewport from processor
        osg::Viewport* vp = new osg::Viewport(*(fp._processor->getCamera()->getViewport()));
        setViewport(vp);
    }

    // check whenever this unit do contain barrier nodes as childs
    for (unsigned int i=0; i < getNumChildren(); i++)
    {
        // if the child is a barrier node, then connect inputs and outputs properly
        BarrierNode* br = dynamic_cast<BarrierNode*>(getChild(i));
        if (br)
        {
            // if the child is not a unit, then there is a bug
            Unit* child = dynamic_cast<Unit*>(br->getBlockedChild());
            if (!child)
            {
                osg::notify(osg::FATAL) << "osgPPU::Unit::setupInputsFromParents() - " << getName() <<" - non valid barrier child!" << std::endl;
                return;
            }
    
            // find next free input slot of the blocked child
            //unsigned int index = 0;
            //for (;;index++)
            //    if (!child->getInputTexture(index) && !child->isInputIndexFixed(index)) break;
    
            // set the output texture of the parent as input texture to the child
            //child->setInputTexture(getOrCreateOutputTexture(0), index);
            
            // add the texture of the blocked parent to the blocked child
            child->mInputTex[child->getNumParents()] = getOrCreateOutputTexture(0);
            child->dirty();
        }
    }

}

//--------------------------------------------------------------------------
void Unit::printDebugInfo() 
{
    osg::NotifySeverity level = osg::INFO;

    // debug information
    osg::notify(level) << getName() << "(" << getIndex() << ")" << std::endl;
    osg::notify(level) << "\t vp (ref " << getInputTextureIndexForViewportReference() << "): " << (int)getViewport()->x() << " " << (int)getViewport()->y() << " " << (int)getViewport()->width() << " " << (int)getViewport()->height() << std::endl;
    osg::notify(level) << "\t shader: " << std::hex << getShader() << std::dec << std::endl;

    if (getShader() != NULL)
    {
        osg::StateSet::UniformList::const_iterator jt = getShader()->getUniformList().begin();
        for (; jt != getShader()->getUniformList().end(); jt++)
        {
            float fval = -1.0;
            int ival = -1;
            if (jt->second.first->getType() == osg::Uniform::INT || jt->second.first->getType() == osg::Uniform::SAMPLER_2D)
            {
                jt->second.first->get(ival);
                osg::notify(level) << "\t\t" << jt->first << " : " << ival << std::endl;//, (jt->second.second & osg::StateAttribute::ON) != 0);
            }else if (jt->second.first->getType() == osg::Uniform::FLOAT){
                jt->second.first->get(fval);
                osg::notify(level) << "\t\t" << jt->first << " : " << fval << std::endl;//, (jt->second.second & osg::StateAttribute::ON) != 0);
            }
        }
    }

    osg::notify(level) << "\t input: ";
    for (unsigned int i=0; i < getInputTextureMap().size(); i++)
    {
        osg::Texture* tex = getInputTexture(i);
        osg::notify(level) << " " << i << ":" << std::hex << tex << std::dec;
        if (tex)
        {
            if (getStateSet()->getTextureAttribute(i, osg::StateAttribute::TEXTURE))
                osg::notify(level) << "-attr";
            osg::notify(level) << " (" << tex->getTextureWidth() << "x" << tex->getTextureHeight() << ")";
        }
    }

    osg::notify(level) << std::endl << "\t output: ";
    for (unsigned int i=0; i < getOutputTextureMap().size(); i++)
    {
        osg::Texture* tex = getOutputTexture(i);
        osg::notify(level) << " " << std::hex << tex << std::dec << " ";
        if (tex) osg::notify(level) << "(" << tex->getTextureWidth() << "x" << tex->getTextureHeight() << " )";
    }

    osg::notify(level) << std::endl;
}

}; // end namespace

