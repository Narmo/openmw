#include "debugging.hpp"

#include <assert.h>

#include <OgreNode.h>
#include <OgreSceneManager.h>

#include "../mwworld/world.hpp" // these includes can be removed once the static-hack is gone
#include "../mwworld/environment.hpp"
#include "../mwworld/ptr.hpp"
#include <components/esm/loadstat.hpp>
#include <components/esm/loadpgrd.hpp>

#include "player.hpp"

using namespace Ogre;

namespace MWRender
{

static const std::string PATHGRID_LINE_MATERIAL = "pathgridLineMaterial";
static const std::string DEBUGGING_GROUP = "debugging";

ManualObject *createPathgridLine(SceneManager* sceneMgr, const Vector3& from, const Vector3& to)
{
    ManualObject *line = sceneMgr->createManualObject();
    if (MaterialManager::getSingleton().getByName(PATHGRID_LINE_MATERIAL, DEBUGGING_GROUP).isNull())
    {
        MaterialPtr lineMatPtr = MaterialManager::getSingleton().create(PATHGRID_LINE_MATERIAL, DEBUGGING_GROUP);
        lineMatPtr->setReceiveShadows(false);
        lineMatPtr->getTechnique(0)->setLightingEnabled(true);
        lineMatPtr->getTechnique(0)->getPass(0)->setDiffuse(1,1,0,0);
        lineMatPtr->getTechnique(0)->getPass(0)->setAmbient(1,1,0);
        lineMatPtr->getTechnique(0)->getPass(0)->setSelfIllumination(1,1,0);
    }

    line->begin(PATHGRID_LINE_MATERIAL, Ogre::RenderOperation::OT_LINE_LIST);
    line->position(from);
    line->position(to);
    line->end();

    return line;
}

ManualObject *createPathgridPoint(SceneManager* sceneMgr, const Vector3& pos)
{
}

Debugging::Debugging(SceneNode *mwRoot, MWWorld::Environment &env, OEngine::Physic::PhysicEngine *engine) :
    mMwRoot(mwRoot), mEnvironment(env), mEngine(engine),
    mSceneMgr(mwRoot->getCreator()),
    pathgridEnabled(false),
    mInteriorPathgridNode(NULL), mPathGridRoot(NULL)
{
    ResourceGroupManager::getSingleton().createResourceGroup(DEBUGGING_GROUP);
}

Debugging::~Debugging()
{
    ResourceGroupManager::getSingleton().destroyResourceGroup(DEBUGGING_GROUP);
}


bool Debugging::toggleRenderMode (int mode){
    switch (mode)
    {
        case MWWorld::World::Render_CollisionDebug:

            // TODO use a proper function instead of accessing the member variable
            // directly.
            mEngine->setDebugRenderingMode (!mEngine->isDebugCreated);
            return mEngine->isDebugCreated;
        case MWWorld::World::Render_Pathgrid:
            togglePathgrid();
            return pathgridEnabled;
    }

    return false;
}

void Debugging::cellAdded(MWWorld::Ptr::CellStore *store)
{
    std::cout << "Cell added to debugging" << std::endl;
    mActiveCells.push_back(store);
    if (pathgridEnabled)
        enableCellPathgrid(store);
}

void Debugging::cellRemoved(MWWorld::Ptr::CellStore *store)
{
    mActiveCells.erase(std::remove(mActiveCells.begin(), mActiveCells.end(), store), mActiveCells.end());
    std::cout << "Cell removed from debugging, active cells count: " << mActiveCells.size() << std::endl;
    if (pathgridEnabled)
        disableCellPathgrid(store);
}

void Debugging::togglePathgrid()
{
    pathgridEnabled = !pathgridEnabled;
    if (pathgridEnabled)
    {
        // add path grid meshes to already loaded cells
        mPathGridRoot = mMwRoot->createChildSceneNode();
        for(CellList::iterator it = mActiveCells.begin(); it != mActiveCells.end(); it++)
        {
            enableCellPathgrid(*it);
        }
    }
    else {
        // remove path grid meshes from already loaded cells
        for(CellList::iterator it = mActiveCells.begin(); it != mActiveCells.end(); it++)
        {
            disableCellPathgrid(*it);
        }
        mPathGridRoot->removeAndDestroyAllChildren();
        mSceneMgr->destroySceneNode(mPathGridRoot);
        mPathGridRoot = NULL;
    }
}

void Debugging::enableCellPathgrid(MWWorld::Ptr::CellStore *store)
{
    ESM::Pathgrid *pathgrid = mEnvironment.mWorld->getStore().pathgrids.search(*store->cell);
    if (!pathgrid)
    {
        return;
    }

    Vector3 cellPathGridPos;
    /// \todo replace tests like this with isExterior method of ESM::Cell after merging with terrain branch
    if (!(store->cell->data.flags & ESM::Cell::Interior))
    {
        /// \todo Replace with ESM::Land::REAL_SIZE after merging with terrain branch
        cellPathGridPos.x = store->cell->data.gridX * 8192;
        cellPathGridPos.y = store->cell->data.gridY * 8192;
    }
    SceneNode *cellPathGrid = mPathGridRoot->createChildSceneNode(cellPathGridPos);
    ESM::Pathgrid::PointList points = pathgrid->points;
    for (ESM::Pathgrid::PointList::iterator it = points.begin(); it != points.end(); it++)
    {
        Vector3 position(it->x, it->y, it->z);
        SceneNode* pointNode = cellPathGrid->createChildSceneNode(position);
        pointNode->setScale(0.5, 0.5, 0.5);
        Entity *pointMesh = mSceneMgr->createEntity(SceneManager::PT_CUBE);
        pointNode->attachObject(pointMesh);
    }

    ESM::Pathgrid::EdgeList edges = pathgrid->edges;
    for(ESM::Pathgrid::EdgeList::const_iterator it = edges.begin();
        it != edges.end(); it++)
    {
        ESM::Pathgrid::Edge edge = *it;
        ESM::Pathgrid::Point p1 = points[edge.v0], p2 = points[edge.v1];
        cellPathGrid->attachObject(createPathgridLine(cellPathGrid->getCreator(),
                                                      Vector3(p1.x, p1.y, p1.z),
                                                      Vector3(p2.x, p2.y, p2.z)));
    }

    if (!(store->cell->data.flags & ESM::Cell::Interior))
    {
        mExteriorPathgridNodes[std::make_pair(store->cell->data.gridX, store->cell->data.gridY)] = cellPathGrid;
    }
    else
    {
        assert(mInteriorPathgridNode == NULL);
        mInteriorPathgridNode = cellPathGrid;
    }
}

void Debugging::disableCellPathgrid(MWWorld::Ptr::CellStore *store)
{
    if (!(store->cell->data.flags & ESM::Cell::Interior))
    {
        ExteriorPathgridNodes::iterator it =
                mExteriorPathgridNodes.find(std::make_pair(store->cell->data.gridX, store->cell->data.gridY));
        if (it != mExteriorPathgridNodes.end())
        {
            destroyCellPathgridNode(it->second);
            mExteriorPathgridNodes.erase(it);
        }
    }
    else
    {
        if (mInteriorPathgridNode)
        {
            destroyCellPathgridNode(mInteriorPathgridNode);
            mInteriorPathgridNode = NULL;
        }
    }
}

void Debugging::destroyCellPathgridNode(SceneNode *node)
{
    mPathGridRoot->removeChild(node);

    /// \todo should object be killed by hand or removeAndDestroyAllChildren is sufficient?
    SceneNode::ChildNodeIterator childIt = node->getChildIterator();
    while (childIt.hasMoreElements())
    {
        SceneNode *child = static_cast<SceneNode *>(childIt.getNext());
        destroyAttachedObjects(child);
    }
    destroyAttachedObjects(node);
    node->removeAndDestroyAllChildren();
    mSceneMgr->destroySceneNode(node);
}

void Debugging::destroyAttachedObjects(SceneNode *node)
{
    SceneNode::ObjectIterator objIt = node->getAttachedObjectIterator();
    while (objIt.hasMoreElements())
    {
        MovableObject *mesh = static_cast<MovableObject *>(objIt.getNext());
        node->getCreator()->destroyMovableObject(mesh);
    }
}

}
