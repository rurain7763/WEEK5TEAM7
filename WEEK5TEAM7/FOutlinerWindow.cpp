#include "FOutlinerWindow.h"
#include "ObjectFactory.h"
#include "Actor.h"
#include "SceneManager.h"
#include "Object.h"
#include "World.h"
#include "FEditorUIManager.h"
#include <algorithm>

void FOutlinerWindow::Render(const FGuiReference& GuiReference)
{
	ImGuiIO& io = ImGui::GetIO();
	UWorld* CurrentWorld = GuiReference.SceneManager->GetCurrentWorld();
	 AActor* SelectedActor = GuiReference.SceneManager->GetSelectedActor();

	ImGuiWindowFlags Flags = ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Object List Panel", nullptr, Flags);
	{
		/* Object Lists */
		ImGui::SeparatorText("Object Lists");
		if (ImGui::BeginChild("ObjectList", ImVec2(0, 0), ImGuiChildFlags_Borders))
		{
			if (mLastGUObjectRevision != UObject::GetGObjectRevision())
			{
				mSortedObjectLists = UObject::GetGObjectArray().ToTArray();
				mLastGUObjectRevision = UObject::GetGObjectRevision();

				// Sort the objects by UUID
				std::sort(mSortedObjectLists.begin(), mSortedObjectLists.end(), [](UObject* a, UObject* b) { return a->UUID < b->UUID; });
			}

			int32 SelectedActorUUID = SelectedActor ? SelectedActor->UUID : -1;

			// Todo: rbegin()
			//for (UObject* object : mGuiInputField.SortedObjectLists)

			UObject* bDeleteActorOrNull = nullptr;
			for (unsigned int objectsIndex = 0; objectsIndex < mSortedObjectLists.Num(); ++objectsIndex)
			{
				UObject* object = mSortedObjectLists[objectsIndex];

				bool bSelected = false;
				ImGui::PushID(object->UUID); // Ensure unique ID for each child

				// Highlight the frame if this object is the clicked actor
				if (object->UUID == SelectedActorUUID)
				{
					bSelected = true;
					ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 0, 50)); // Light yellow background
				}

				if (ImGui::BeginChild("ObjectFrame", ImVec2(0, 0), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY))
				{
					ImGui::Text("Class: %s", object->GetClass()->Name.CStr());
					ImGui::Text("UUID: %d", object->UUID);

					// TODO: Move implement delete to where?
					if (object->IsA<AActor>())
					{
						AActor* actor = object->Cast<AActor>();

						if (ImGui::Button("Select"))
						{
							GuiReference.SceneManager->SetSelectedActor(actor);
						}
						else
						{
							ImGui::SameLine();
							if (ImGui::Button("Delete"))
							{
								bDeleteActorOrNull = object;
							}
						}
					}
				}
				ImGui::EndChild();

				if (bSelected)
				{
					ImGui::PopStyleColor(); // Pop the border color if it was pushed
				}


				ImGui::PopID();
			}

			if (bDeleteActorOrNull != nullptr)
			{
				AActor* deleteActor = bDeleteActorOrNull->Cast<AActor>();

				if (GuiReference.SceneManager->GetSelectedActor() == deleteActor)
				{
					GuiReference.SceneManager->ResetSelectedActor();
				}

				assert(CurrentWorld != nullptr);
				CurrentWorld->RemoveActor(deleteActor->UUID);

				FObjectFactory::DestroyObject(deleteActor);
			}
		}
		ImGui::EndChild();
	}

	ImGui::End();
}
