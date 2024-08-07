
#include "AsyncVTKDemo.h"

#include "vtkAppendDataSets.h"
#include "vtkCamera.h"
#include "vtkCylinderSource.h"
#include "vtkDataSetMapper.h"
#include "vtkImplicitPlaneRepresentation.h"
#include "vtkImplicitPlaneWidget2.h"
#include "vtkInteractorStyleSwitch.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkSMPTools.h"
#include "vtkUnstructuredGrid.h"

#include <emscripten/eventloop.h>
#include <emscripten/stack.h>
#include <emscripten/threading.h>

#include <map>
#include <thread>

#define LOG(msg)                                                                                   \
  do                                                                                               \
  {                                                                                                \
    if (::threadNames.find(pthread_self()) != ::threadNames.end())                                 \
    {                                                                                              \
      std::cout << "=> (" << ::threadNames[pthread_self()] << ") " << msg << '\n';                 \
    }                                                                                              \
    else                                                                                           \
    {                                                                                              \
      std::cout << "=> (0x" << std::hex << pthread_self() << std::dec << ") " << msg << '\n';      \
    }                                                                                              \
  } while (0)

namespace
{
std::map<pthread_t, std::string> threadNames;
vtkSmartPointer<vtkUnstructuredGrid> BuildUnstructuredGrid()
{
  vtkNew<vtkAppendDataSets> cylinders;
  double dx = 0, dy = 0, dz = 0;
  for (int i = 0; i < 10; ++i)
  {
    dy = 0;
    for (int j = 0; j < 10; ++j)
    {
      dz = 0;
      for (int k = 0; k < 10; ++k)
      {
        vtkNew<vtkCylinderSource> cylinder;
        cylinder->SetResolution(500);
        cylinder->SetCenter(dx, dy, dz);
        cylinders->AddInputConnection(cylinder->GetOutputPort());
        dz += 2;
      }
      dy += 2;
    }
    dx += 2;
  }
  cylinders->SetOutputDataSetType(VTK_UNSTRUCTURED_GRID);
  cylinders->Update();
  return vtkUnstructuredGrid::SafeDownCast(cylinders->GetOutput());
}
} // namespace

//---------------------------------------------------------------------
AsyncVTKDemo::AsyncVTKDemo(std::string canvasId)
  : m_CanvasId(canvasId)
  , m_Started(false)
{
}

//---------------------------------------------------------------------
AsyncVTKDemo::~AsyncVTKDemo() = default;

//---------------------------------------------------------------------
void AsyncVTKDemo::Abort()
{
  LOG(__func__);
  if (this->m_Clipper != nullptr)
  {
    this->m_Clipper->SetAbortExecuteAndUpdateTime();
  }
}

//---------------------------------------------------------------------
void AsyncVTKDemo::UpdateClipPlaneNormal(double nx, double ny, double nz)
{
  LOG(__func__);
  if (this->m_ClipPlane != nullptr)
  {
    this->m_ClipPlane->SetNormal(nx, ny, nz);
  }
  // Also reorient the plane widget
  if (this->m_PlaneWidget != nullptr)
  {
    auto* rep =
      reinterpret_cast<vtkImplicitPlaneRepresentation*>(this->m_PlaneWidget->GetRepresentation());
    rep->SetPlane(this->m_ClipPlane);
  }
}

//---------------------------------------------------------------------
void AsyncVTKDemo::SyncRender()
{
  LOG(__func__);
  if (this->m_RenderWindow != nullptr)
  {
    this->m_Queue.proxySync(this->m_RenderThread,
      [this]()
      {
        LOG("AsyncVTKDemo::RenderWindow::Render");
        // Clear abort execute flag in case filter was aborted.
        this->m_Clipper->SetAbortExecute(false);
        this->m_RenderWindow->Render();
      });
  }
}

//---------------------------------------------------------------------
void AsyncVTKDemo::AsyncRender()
{
  LOG(__func__);
  if (this->m_RenderWindow != nullptr)
  {
    this->m_Queue.proxyAsync(this->m_RenderThread,
      [this]()
      {
        LOG("AsyncVTKDemo::vtkRenderWindow::Render");
        // Clear abort execute flag in case filter was aborted.
        this->m_Clipper->SetAbortExecute(false);
        this->m_RenderWindow->Render();
      });
  }
}

//---------------------------------------------------------------------
int AsyncVTKDemo::Start()
{
  LOG(__func__);
  if (this->m_Started)
  {
    return 0;
  }
  // This is the main thread for the application.
  this->m_UIThread = pthread_self();
  ::threadNames[this->m_UIThread] = "UI";
  // For thread profiler
  emscripten_set_thread_name(pthread_self(), "UI");

  vtkSMPTools::Initialize();
  std::cout << "VTK SMP Backend: " << vtkSMPTools::GetBackend()
            << " with " << vtkSMPTools::GetEstimatedNumberOfThreads() << " thread (s)\n";

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  // Use the size of the current stack, which is the normal size of the stack
  // that main thread gets.
  pthread_attr_setstacksize(&attr, emscripten_stack_get_base() - emscripten_stack_get_end());
  if (!this->m_CanvasId.empty())
  {
    LOG("Transferring canvas " << this->m_CanvasId << " offscreen");
    emscripten_pthread_attr_settransferredcanvases(&attr, this->m_CanvasId.c_str());
  }
  else
  {
    // Pass special ID -1 to the list of transferred canvases to denote that the
    // thread creation should instead take a list of canvases that are specified
    // from the command line with -sOFFSCREENCANVASES_TO_PTHREAD linker flag.
    emscripten_pthread_attr_settransferredcanvases(&attr, (const char*)-1);
  }
  int rc = pthread_create(&this->m_RenderThread, &attr, AsyncVTKDemo::StartRendering, this);
  pthread_attr_destroy(&attr);
  return rc;
}

//---------------------------------------------------------------------
void* AsyncVTKDemo::StartRendering(void* userdata)
{
  LOG(__func__);
  // This is the rendering thread for the application.
  threadNames[pthread_self()] = "Render";
  // For thread profiler
  emscripten_set_thread_name(pthread_self(), "Render");
  vtkRenderWindowInteractor::InteractorManagesTheEventLoop = false;

  auto* self = reinterpret_cast<AsyncVTKDemo*>(userdata);

  auto inputMesh = BuildUnstructuredGrid();

  double bounds[6] = {};
  inputMesh->GetBounds(bounds);
  double origin[3] = { 0.5 * (bounds[0] + bounds[1]), 0.5 * (bounds[2] + bounds[3]),
    0.5 * (bounds[4] + bounds[5]) };

  self->m_Clipper = vtk::TakeSmartPointer(vtkTableBasedClipDataSet::New());
  self->m_ClipPlane = vtk::TakeSmartPointer(vtkPlane::New());
  self->m_ClipPlane->SetOrigin(origin);
  self->m_Clipper->SetClipFunction(self->m_ClipPlane);
  self->m_Clipper->SetInputDataObject(inputMesh);

  vtkNew<vtkDataSetMapper> mapper;
  mapper->SetInputConnection(self->m_Clipper->GetOutputPort());

  self->m_Actor = vtk::TakeSmartPointer(vtkActor::New());
  self->m_Actor->SetMapper(mapper);

  vtkNew<vtkRenderer> renderer;
  renderer->AddActor(self->m_Actor);
  renderer->ResetCamera();

  self->m_RenderWindow = vtk::TakeSmartPointer(vtkRenderWindow::New());
  self->m_RenderWindow->AddRenderer(renderer);
  self->m_RenderWindow->Render();

  self->m_Interactor = vtk::TakeSmartPointer(vtkRenderWindowInteractor::New());
  self->m_Interactor->SetRenderWindow(self->m_RenderWindow);
  auto* istyleSwitch =
    vtkInteractorStyleSwitch::SafeDownCast(self->m_Interactor->GetInteractorStyle());
  istyleSwitch->SetCurrentStyleToTrackballCamera();

  vtkNew<vtkImplicitPlaneRepresentation> planeWidgetRep;
  planeWidgetRep->SetPlaceFactor(1.25);
  planeWidgetRep->PlaceWidget(inputMesh->GetBounds());
  planeWidgetRep->SetPlane(self->m_ClipPlane);
  planeWidgetRep->SetDrawOutline(false);

  self->m_PlaneWidget = vtk::TakeSmartPointer(vtkImplicitPlaneWidget2::New());
  self->m_PlaneWidget->SetInteractor(self->m_Interactor);
  self->m_PlaneWidget->SetRepresentation(planeWidgetRep);
  self->m_PlaneWidget->AddObserver(
    vtkCommand::InteractionEvent, self, &AsyncVTKDemo::OnClipPlaneInteraction);
  self->m_PlaneWidget->On();

  if (self->m_ClipPlaneCmd)
  {
    self->m_ClipPlaneObserverTag =
      self->m_ClipPlane->AddObserver(vtkCommand::ModifiedEvent, self->m_ClipPlaneCmd);
  }

  self->m_Interactor->Start();

  self->m_Queue.proxyAsync(self->m_UIThread, [](){
    EM_ASM(window.dispatchEvent(new Event('resize')));
  });

  LOG("Started event loop");

  return nullptr;
}

namespace
{
struct CallbackOnThreadBridge
{
  emscripten::ProxyingQueue* Queue;
  pthread_t* Target;
  AsyncVTKDemo::ClipPlaneModifiedCallbackType Call;
};
} // namespace

//---------------------------------------------------------------------
void AsyncVTKDemo::AddClipPlaneModifiedUIObserver(ClipPlaneModifiedCallbackType callback)
{
  LOG(__func__);

  auto* bridge = new CallbackOnThreadBridge();
  bridge->Call = callback;
  bridge->Target = &(this->m_UIThread);
  bridge->Queue = &(this->m_Queue);

  this->m_ClipPlaneCmd = vtk::TakeSmartPointer(vtkCallbackCommand::New());
  this->m_ClipPlaneCmd->SetClientData(bridge);
  this->m_ClipPlaneCmd->SetClientDataDeleteCallback(
    [](void* clientdata)
    {
      if (auto* _bridge = reinterpret_cast<CallbackOnThreadBridge*>(clientdata))
      {
        delete _bridge;
      }
    });
  this->m_ClipPlaneCmd->SetCallback(
    [](vtkObject* caller, unsigned long, void* clientdata, void*)
    {
      if (auto* _bridge = reinterpret_cast<CallbackOnThreadBridge*>(clientdata))
      {
        auto* plane = reinterpret_cast<vtkPlane*>(caller);
        _bridge->Queue->proxyAsync(*(_bridge->Target),
          [plane, f = _bridge->Call]()
          {
            double n[3] = {};
            plane->GetNormal(n);
            f(n[0], n[1], n[2]);
          });
      }
    });
}

//---------------------------------------------------------------------
void AsyncVTKDemo::OnClipPlaneInteraction(vtkObject* caller, unsigned long, void*)
{
  auto* planeWidget = reinterpret_cast<vtkImplicitPlaneWidget2*>(caller);
  auto* rep = reinterpret_cast<vtkImplicitPlaneRepresentation*>(planeWidget->GetRepresentation());
  rep->GetPlane(vtkPlane::SafeDownCast(this->m_Clipper->GetClipFunction()));
  // clear abort flag
  this->m_Clipper->SetAbortExecute(false);
}
