//
// Copyright (c) 2018 Rokas Kupstys
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/Graphics/RenderPath.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Toolbox/SystemUI/Widgets.h>
#include <IconFontCppHeaders/IconsFontAwesome5.h>
#include <ImGui/imgui_stl.h>
#include <Urho3D/IO/Log.h>
#include "MaterialInspector.h"
#include "MaterialInspectorUndo.h"

using namespace ImGui::litterals;

namespace Urho3D
{

MaterialInspector::MaterialInspector(Context* context, Material* material)
    : ResourceInspector(context)
    , inspectable_(new Inspectable::Material(material))
    , view_(context, {0, 0, 200, 200})
    , attributeInspector_(context)
{
    // Workaround: for some reason this overriden method of our class does not get called by SceneView constructor.
    CreateObjects();

    // Scene viewport renderpath must be same as material viewport renderpath
    Viewport* effectSource = nullptr;
    if (effectSource != nullptr)
    {
        // TODO: Load material previouw effects configuration from active scene viewport
        auto path = effectSource->GetRenderPath();
        view_.GetViewport()->SetRenderPath(path);
        auto light = view_.GetCamera()->GetComponent<Light>();
        for (auto& command: path->commands_)
        {
            if (command.pixelShaderName_ == "PBRDeferred")
            {
                // Lights in PBR scenes need modifications, otherwise obects in material preview look very dark
                light->SetUsePhysicalValues(true);
                light->SetBrightness(5000);
                light->SetShadowCascade(CascadeParameters(10, 20, 30, 40, 10));
                break;
            }
        }
    }

    auto autoSave = [this](StringHash, VariantMap&) {
        // Auto-save material on modification
        auto* material = inspectable_->GetMaterial();
        GetCache()->IgnoreResourceReload(material);
        material->SaveFile(GetCache()->GetResourceFileName(material->GetName()));
    };
    SubscribeToEvent(&attributeInspector_, E_ATTRIBUTEINSPECTVALUEMODIFIED, autoSave);
    SubscribeToEvent(&attributeInspector_, E_INSPECTORRENDERSTART, [this](StringHash, VariantMap&) { RenderPreview(); });
    SubscribeToEvent(&attributeInspector_, E_INSPECTORRENDERATTRIBUTE, [this](StringHash, VariantMap& args) { RenderCustomWidgets(args); });

    undo_.Connect(&attributeInspector_);
}

void MaterialInspector::RenderInspector(const char* filter)
{
    RenderAttributes(inspectable_, filter, &attributeInspector_);
}

void MaterialInspector::ToggleModel()
{
    auto model = node_->GetOrCreateComponent<StaticModel>();

    model->SetModel(node_->GetCache()->GetResource<Model>(ToString("Models/%s.mdl", figures_[figureIndex_])));
    model->SetMaterial(inspectable_->GetMaterial());
    auto bb = model->GetBoundingBox();
    auto scale = 1.f / Max(bb.Size().x_, Max(bb.Size().y_, bb.Size().z_));
    if (strcmp(figures_[figureIndex_], "Box") == 0)            // Box is rather big after autodetecting scale, but other
        scale *= 0.7f;                                         // figures are ok. Patch the box then.
    else if (strcmp(figures_[figureIndex_], "TeaPot") == 0)    // And teapot is rather small.
        scale *= 1.2f;
    node_->SetScale(scale);
    node_->SetWorldPosition(node_->GetWorldPosition() - model->GetWorldBoundingBox().Center());

    figureIndex_ = ++figureIndex_ % figures_.Size();
}

void MaterialInspector::SetGrab(bool enable)
{
    if (mouseGrabbed_ == enable)
        return;

    mouseGrabbed_ = enable;
    Input* input = view_.GetCamera()->GetInput();
    if (enable && input->IsMouseVisible())
        input->SetMouseVisible(false);
    else if (!enable && !input->IsMouseVisible())
        input->SetMouseVisible(true);
}

void MaterialInspector::CreateObjects()
{
    view_.CreateObjects();
    node_ = view_.GetScene()->CreateChild("Sphere");
    ToggleModel();
    view_.GetCamera()->GetNode()->CreateComponent<Light>();
    view_.GetCamera()->GetNode()->SetPosition(Vector3::BACK * distance_);
    view_.GetCamera()->GetNode()->LookAt(Vector3::ZERO);
}

void MaterialInspector::Save()
{
    inspectable_->GetMaterial()->SaveFile(GetCache()->GetResourceFileName(inspectable_->GetMaterial()->GetName()));
}

void MaterialInspector::RenderPreview()
{
    auto size = static_cast<int>(ui::GetWindowWidth() - ui::GetCursorPosX());
    view_.SetSize({0, 0, size, size});
    ui::Image(view_.GetTexture(), ImVec2(view_.GetTexture()->GetWidth(), view_.GetTexture()->GetHeight()));
    ui::SetHelpTooltip("Click to switch object.");
    Input* input = view_.GetCamera()->GetInput();
    bool rightMouseButtonDown = input->GetMouseButtonDown(MOUSEB_RIGHT);
    if (ui::IsItemHovered())
    {
        if (rightMouseButtonDown)
            SetGrab(true);
        else if (input->GetMouseButtonPress(MOUSEB_LEFT))
            ToggleModel();
    }

    if (mouseGrabbed_)
    {
        if (rightMouseButtonDown)
        {
            if (input->GetKeyPress(KEY_ESCAPE))
            {
                view_.GetCamera()->GetNode()->SetPosition(Vector3::BACK * distance_);
                view_.GetCamera()->GetNode()->LookAt(Vector3::ZERO);
            }
            else
            {
                IntVector2 delta = input->GetMouseMove();
                view_.GetCamera()->GetNode()->RotateAround(Vector3::ZERO,
                    Quaternion(delta.x_ * 0.1f, view_.GetCamera()->GetNode()->GetUp()) *
                    Quaternion(delta.y_ * 0.1f, view_.GetCamera()->GetNode()->GetRight()), TS_WORLD);
            }
        }
        else
            SetGrab(false);
    }

    const char* resourceName = inspectable_->GetMaterial()->GetName().CString();
    ui::SetCursorPosX((ui::GetContentRegionMax().x - ui::CalcTextSize(resourceName).x) / 2);
    ui::TextUnformatted(resourceName);
    ui::Separator();
}

void MaterialInspector::RenderCustomWidgets(VariantMap& args)
{
    using namespace InspectorRenderAttribute;
    AttributeInfo& info = *reinterpret_cast<AttributeInfo*>(args[P_ATTRIBUTEINFO].GetVoidPtr());
    Material* material = (dynamic_cast<Inspectable::Material*>(args[P_SERIALIZABLE].GetPtr()))->GetMaterial();
    auto* state = reinterpret_cast<AttributeInspectorState*>(args[P_STATE].GetVoidPtr());

    if (info.name_ == "Depth Bias")
    {
        ui::NewLine();
        ui::IndentScope indentScope(15_dpx);

        BiasParameters bias = material->GetDepthBias();

        ui::TextUnformatted("Constant Bias");
        state->NextColumn();
        bool modified = ui::DragFloat("###Constant Bias", &bias.constantBias_, 0.01f, -1, 1);

        ui::TextUnformatted("Slope Scaled Bias");
        state->NextColumn();
        modified |= ui::DragFloat("###Slope Scaled Bias", &bias.slopeScaledBias_, 0.01f, -16, 16);

        ui::TextUnformatted("Normal Offset");
        state->NextColumn();
        modified |= ui::DragFloat("###Normal Offset", &bias.normalOffset_, 0.01f, 0, M_INFINITY);

        // Track undo
        auto* modification = ui::GetUIState<ModifiedStateTracker<BiasParameters>>();
        if (modification->TrackModification(modified, [material]() { return material->GetDepthBias(); }))
            undo_.Track<Undo::DepthBiasAction>(material, modification->GetInitialValue(), bias);

        // Always accept modified values
        if (modified)
            material->SetDepthBias(bias);

        args[P_HANDLED] = true;
    }
    else if (info.name_ == "Techniques")
    {
        ui::NewLine();

        ui::IndentScope indentScope(15_dpx);

        bool modified = false;
        for (unsigned i = 0; i < material->GetNumTechniques(); i++)
        {
            if (i > 0)
                ui::Separator();

            ui::IdScope techniqueIdScope(i);
            auto tech = material->GetTechniqueEntry(i);
            auto modification = ui::GetUIState<ModifiedStateTracker<TechniqueEntry>>();

            String techName = tech.technique_->GetName();
            ui::PushItemWidth(material->GetNumTechniques() > 1 ? -44_dpx : -22_dpx);
            ui::InputText("###techniqueName_", const_cast<char*>(techName.CString()), techName.Length(),
                          ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ReadOnly);
            ui::PopItemWidth();

            if (ui::BeginDragDropTarget())
            {
                const Variant& payload = ui::AcceptDragDropVariant("path");
                if (!payload.IsEmpty())
                {
                    auto* technique = GetCache()->GetResource<Technique>(payload.GetString());
                    if (technique)
                    {
                        material->SetTechnique(i, technique, tech.qualityLevel_, tech.lodDistance_);
                        undo_.Track<Undo::TechniqueChangedAction>(material, i, &tech, &material->GetTechniqueEntry(i));
                        modified = true;
                    }
                }
                ui::EndDragDropTarget();
            }
            ui::SetHelpTooltip("Drag resource here.");

            ui::SameLine(VAR_NONE);
            if (ui::IconButton(ICON_FA_CROSSHAIRS))
            {
                SendEvent(E_INSPECTORLOCATERESOURCE, InspectorLocateResource::P_NAME, material->GetTechnique(i)->GetName());
            }
            ui::SetHelpTooltip("Locate resource");

            if (material->GetNumTechniques() > 1)
            {
                ui::SameLine(VAR_NONE);
                if (ui::IconButton(ICON_FA_TRASH))
                {
                    for (auto j = i + 1; j < material->GetNumTechniques(); j++)
                        material->SetTechnique(j - 1, material->GetTechnique(j));
                    undo_.Track<Undo::TechniqueChangedAction>(material, i, &tech, nullptr);
                    // Technique removed possibly from the middle. Shift existing techniques back to the front and remove last one.
                    for (auto j = i + 1; j < material->GetNumTechniques(); j++)
                    {
                        const auto& entry = material->GetTechniqueEntry(j);
                        material->SetTechnique(j - 1, entry.original_.Get(), entry.qualityLevel_, entry.lodDistance_);
                    }
                    material->SetNumTechniques(material->GetNumTechniques() - 1);
                    modified = true;
                    break;
                }
            }

            // ---------------------------------------------------------------------------------------------------------

            ui::TextUnformatted("LOD Distance");
            state->NextColumn();
            bool modifiedField = ui::DragFloat("###LOD Distance", &tech.lodDistance_);

            // ---------------------------------------------------------------------------------------------------------

            static const char* qualityNames[] = {
                "Low", "Medium", "High", "Ultra", "Max"
            };

            ui::TextUnformatted("Quality");
            state->NextColumn();
            modifiedField |= ui::Combo("###Quality", reinterpret_cast<int*>(&tech.qualityLevel_), qualityNames, SDL_arraysize(qualityNames));

            if (modification->TrackModification(modifiedField, [material, i]() { return material->GetTechniqueEntry(i); }))
                undo_.Track<Undo::TechniqueChangedAction>(material, i, &modification->GetInitialValue(), &tech);

            if (modifiedField)
                material->SetTechnique(i, tech.original_.Get(), tech.qualityLevel_, tech.lodDistance_);

            modified |= modifiedField;
        }

        const char addNewTechnique[]{"Add New Technique"};
        ui::InputText("###Add Technique", const_cast<char*>(addNewTechnique), sizeof(addNewTechnique),
            ImGuiInputTextFlags_ReadOnly);
        if (ui::BeginDragDropTarget())
        {
            const Variant& payload = ui::AcceptDragDropVariant("path");
            if (!payload.IsEmpty())
            {
                auto* technique = GetCache()->GetResource<Technique>(payload.GetString());
                if (technique)
                {
                    auto index = material->GetNumTechniques();
                    material->SetNumTechniques(index + 1);
                    material->SetTechnique(index, technique);
                    undo_.Track<Undo::TechniqueChangedAction>(material, index, nullptr, &material->GetTechniqueEntry(index));
                    modified = true;
                }
            }
            ui::EndDragDropTarget();
        }
        ui::SetHelpTooltip("Drag and drop technique resource here.");

        args[P_HANDLED] = true;
        args[P_MODIFIED] = modified;
    }
    else if (info.name_ == "Shader Parameters")
    {
        struct ShaderParameterState
        {
            std::string fieldName_;
            int variantTypeIndex_ = 0;
            bool insertingNew_ = false;
        };

        auto* paramState = ui::GetUIState<ShaderParameterState>();
        if (ui::Button(ICON_FA_PLUS))
            paramState->insertingNew_ = true;
        ui::SetHelpTooltip("Add new shader parameter.");

        ui::IndentScope indentScope(15_dpx);
        bool modified = false;

        const auto& parameters = material->GetShaderParameters();
        for (const auto& pair : parameters)
        {
            const String& parameterName = pair.second_.name_;
            ui::IdScope id(parameterName.CString());
            auto* modification = ui::GetUIState<ModifiedStateTracker<Variant>>();

            ui::TextUnformatted(parameterName.CString());
            state->NextColumn();
            Variant value = pair.second_.value_;

            UI_ITEMWIDTH(-22_dpx)
            {
                bool modifiedNow = RenderSingleAttribute(state, value);
                if (modification->TrackModification(modifiedNow, [material, &parameterName]() { return material->GetShaderParameter(parameterName); }))
                {
                    undo_.Track<Undo::ShaderParameterChangedAction>(material, parameterName, modification->GetInitialValue(), value);
                    modified = true;
                }
                if (modifiedNow)
                    material->SetShaderParameter(parameterName, value);
            }

            ui::SameLine(value.GetType());
            if (ui::Button(ICON_FA_TRASH))
            {
                undo_.Track<Undo::ShaderParameterChangedAction>(material, parameterName, pair.second_.value_, Variant{});
                material->RemoveShaderParameter(parameterName);
                modified = true;
                break;
            }
        }


        if (paramState->insertingNew_)
        {
            static const VariantType shaderParameterVariantTypes[] = {
                VAR_FLOAT,
                VAR_VECTOR2,
                VAR_VECTOR3,
                VAR_VECTOR4,
                VAR_COLOR,
                VAR_RECT,
            };

            static const char* shaderParameterVariantNames[] = {
                "Float",
                "Vector2",
                "Vector3",
                "Vector4",
                "Color",
                "Rect",
            };

            auto firstColumnWidth = ui::GetContentRegionMax().x - (ui::GetContentRegionMax().x - state->autoColumn_.currentMaxWidth_) - ui::GetCursorPosX();
            //auto secondColumn = ui::GetContentRegionMax().x - firstColumnWidth - 15_dpx;
            UI_ITEMWIDTH(firstColumnWidth)
                ui::InputText("###Name", &paramState->fieldName_);
            ui::SetHelpTooltip("Shader parameter name.");

            state->NextColumn();
            UI_ITEMWIDTH(-22_dpx) // Space for OK button
                ui::Combo("###Type", &paramState->variantTypeIndex_, shaderParameterVariantNames, SDL_arraysize(shaderParameterVariantTypes));
            ui::SetHelpTooltip("Shader parameter type.");

            ui::SameLine(0, 2_dpx);
            if (ui::Button(ICON_FA_CHECK))
            {
                if (material->GetShaderParameter(paramState->fieldName_.c_str()).GetType() == VAR_NONE)   // TODO: Show warning about duplicate name
                {
                    Variant value{shaderParameterVariantTypes[paramState->variantTypeIndex_]};
                    undo_.Track<Undo::ShaderParameterChangedAction>(material, paramState->fieldName_.c_str(), Variant{}, value);
                    material->SetShaderParameter(paramState->fieldName_.c_str(), value);
                    modified = true;
                    paramState->fieldName_.clear();
                    paramState->variantTypeIndex_ = 0;
                    paramState->insertingNew_ = false;
                }
            }
        }

        args[P_HANDLED] = true;
        args[P_MODIFIED] = modified;
    }
}

Inspectable::Material::Material(Urho3D::Material* material)
    : Serializable(material->GetContext())
    , material_(material)
{
}

void Inspectable::Material::RegisterObject(Context* context)
{
    // Cull Mode
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)       { value = inspectable.GetMaterial()->GetCullMode(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value) { inspectable.GetMaterial()->SetCullMode(static_cast<CullMode>(value.GetUInt())); };
        URHO3D_CUSTOM_ENUM_ATTRIBUTE("Cull", getter, setter, cullModeNames, CULL_NONE, AM_EDIT);
    }

    // Shadow Cull Mode
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)       { value = inspectable.GetMaterial()->GetShadowCullMode(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value) { inspectable.GetMaterial()->SetShadowCullMode(static_cast<CullMode>(value.GetUInt())); };
        URHO3D_CUSTOM_ENUM_ATTRIBUTE("Shadow Cull", getter, setter, cullModeNames, CULL_NONE, AM_EDIT);
    }

    // Fill Mode
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)       { value = inspectable.GetMaterial()->GetFillMode(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value) { inspectable.GetMaterial()->SetFillMode(static_cast<FillMode>(value.GetUInt())); };
        URHO3D_CUSTOM_ENUM_ATTRIBUTE("Fill", getter, setter, fillModeNames, FILL_SOLID, AM_EDIT);
    }

    // Alpha To Coverage
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)       { value = inspectable.GetMaterial()->GetAlphaToCoverage(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value) { inspectable.GetMaterial()->SetAlphaToCoverage(value.GetBool()); };
        URHO3D_CUSTOM_ATTRIBUTE("Alpha To Coverage", getter, setter, bool, false, AM_EDIT);
    }

    // Line Anti Alias
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)       { value = inspectable.GetMaterial()->GetLineAntiAlias(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value) { inspectable.GetMaterial()->SetLineAntiAlias(value.GetBool()); };
        URHO3D_CUSTOM_ATTRIBUTE("Line Anti Alias", getter, setter, bool, false, AM_EDIT);
    }

    // Render Order
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)       { value = inspectable.GetMaterial()->GetRenderOrder(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value) { inspectable.GetMaterial()->SetRenderOrder(static_cast<unsigned char>(value.GetUInt())); };
        URHO3D_CUSTOM_ATTRIBUTE("Render Order", getter, setter, unsigned, DEFAULT_RENDER_ORDER, AM_EDIT);
    }

    // Occlusion
    {
        auto getter = [](const Inspectable::Material& inspectable, Variant& value)        { value = inspectable.GetMaterial()->GetOcclusion(); };
        auto setter = [](const Inspectable::Material& inspectable, const Variant& value)  { inspectable.GetMaterial()->SetOcclusion(value.GetBool()); };
        URHO3D_CUSTOM_ATTRIBUTE("Occlusion", getter, setter, bool, false, AM_EDIT);
    }

    // Dummy attributes used for rendering custom widgets in inspector.
    URHO3D_CUSTOM_ATTRIBUTE("Depth Bias", [](const Inspectable::Material&, Variant&){}, [](const Inspectable::Material&, const Variant&){}, unsigned, Variant{}, AM_EDIT);
    URHO3D_CUSTOM_ATTRIBUTE("Techniques", [](const Inspectable::Material&, Variant&){}, [](const Inspectable::Material&, const Variant&){}, unsigned, Variant{}, AM_EDIT);
    URHO3D_CUSTOM_ATTRIBUTE("Shader Parameters", [](const Inspectable::Material&, Variant&){}, [](const Inspectable::Material&, const Variant&){}, unsigned, Variant{}, AM_EDIT);

    for (auto i = 0; i < MAX_MATERIAL_TEXTURE_UNITS; i++)
    {
        String finalName = ToString("%s Texture", textureUnitNames[i]);
        finalName.ReplaceUTF8(0, ToUpper(finalName.AtUTF8(0)));
        auto textureUnit = static_cast<TextureUnit>(i);

        auto getter = [textureUnit](const Inspectable::Material& inspectable, Variant& value)
        {
            if (auto* texture = inspectable.GetMaterial()->GetTexture(textureUnit))
                value = ResourceRef(Texture2D::GetTypeStatic(), texture->GetName());
            else
                value = ResourceRef{Texture2D::GetTypeStatic()};
        };
        auto setter = [textureUnit](const Inspectable::Material& inspectable, const Variant& value)
        {
            const auto& ref = value.GetResourceRef();
            auto* texture = dynamic_cast<Texture*>(inspectable.GetCache()->GetResource(ref.type_, ref.name_));
            inspectable.GetMaterial()->SetTexture(textureUnit, texture);
        };
        URHO3D_CUSTOM_ATTRIBUTE(finalName.CString(), getter, setter, ResourceRef, ResourceRef{Texture2D::GetTypeStatic()}, AM_EDIT);
    }
}

}