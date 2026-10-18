/*
    FunshiEngineCLI - CLI de automación del motor FunshiEngineGL
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
#include "Escena/SerializacionEscena.h"

#include <Objetos/Componentes/Color.h>
#include <Objetos/Componentes/Model.h>
#include <Objetos/Componentes/Script.h>
#include <Objetos/Componentes/Transform.h>

#include <functional>
#include <typeinfo>

namespace funshi {

nlohmann::json componenteJson(Component* comp) {
    nlohmann::json result;
    result["type"] = typeid(*comp).name();
    std::string typeName = typeid(*comp).name();
    if (typeName.rfind("class ", 0) == 0) typeName = typeName.substr(6);
    else if (typeName.rfind("struct ", 0) == 0) typeName = typeName.substr(7);
    result["typeName"] = typeName;

    if (auto* transform = dynamic_cast<Transform*>(comp)) {
        float* pos = transform->getTranslatef();
        float* rot = transform->getRotatef();
        float* scale = transform->getScalef();
        result["position"] = {pos[0], pos[1], pos[2]};
        result["rotation"] = {rot[0], rot[1], rot[2], rot[3]};
        result["scale"] = {scale[0], scale[1], scale[2]};
    } else if (auto* script = dynamic_cast<Script*>(comp)) {
        result["scriptPath"] = script->getPath();
        result["className"] = script->getNameClass();
        nlohmann::json campos = nlohmann::json::array();
        for (const auto& def : script->obtenerCampos()) {
            nlohmann::json campo;
            campo["name"] = def.nombre;
            campo["type"] = static_cast<int>(def.tag);
            campos.push_back(std::move(campo));
        }
        result["serializeFields"] = std::move(campos);
    } else if (auto* model = dynamic_cast<Model*>(comp)) {
        result["modelPath"] = model->getPath();
    } else if (auto* color = dynamic_cast<Color*>(comp)) {
        const float* c = color->getColor();
        if (c) result["color"] = {c[0], c[1], c[2], c[3]};
    }
    // Otros componentes: solo el tipo.

    return result;
}

nlohmann::json gameObjectJson(GameObject* obj) {
    nlohmann::json result;
    result["id"] = obj->getId();
    result["name"] = obj->inputName;
    result["tag"] = obj->getTag();
    result["active"] = obj->getState();

    // Transform local del objeto.
    if (auto* transform = obj->getComponent<Transform>()) {
        float* pos = transform->getTranslatef();
        float* rot = transform->getRotatef();
        float* scale = transform->getScalef();
        result["transform"] = {
            {"position", {pos[0], pos[1], pos[2]}},
            {"rotation", {rot[0], rot[1], rot[2], rot[3]}},
            {"scale", {scale[0], scale[1], scale[2]}}
        };
    }

    nlohmann::json components = nlohmann::json::array();
    if (auto* componentsList = obj->getComponents()) {
        if (!componentsList->isEmpty()) {
            auto* pos = componentsList->first();
            while (pos != nullptr) {
                components.push_back(componenteJson(pos->getElement()));
                pos = (pos != componentsList->last()) ? componentsList->next(pos) : nullptr;
            }
        }
    }
    result["components"] = std::move(components);

    nlohmann::json children = nlohmann::json::array();
    for (Entity* child : obj->getChildEntities()) {
        if (auto* go = dynamic_cast<GameObject*>(child)) {
            children.push_back(gameObjectJson(go));
        }
    }
    result["children"] = std::move(children);

    return result;
}

nlohmann::json sceneHierarchyJson(SceneRegistry* registry) {
    GameObject* root = registry->getRoot();
    if (!root) return nlohmann::json::array();

    nlohmann::json result = nlohmann::json::array();
    for (Entity* child : root->getChildEntities()) {
        if (auto* go = dynamic_cast<GameObject*>(child)) {
            result.push_back(gameObjectJson(go));
        }
    }
    return result;
}

GameObject* findObjectById(SceneRegistry* registry, int id) {
    if (id == 0) return registry->getRoot();
    GameObject* root = registry->getRoot();
    if (!root) return nullptr;

    // Búsqueda recursiva por id.
    std::function<GameObject*(GameObject*)> search = [&](GameObject* obj) -> GameObject* {
        if (obj->getId() == id) return obj;
        for (Entity* child : obj->getChildEntities()) {
            if (auto* go = dynamic_cast<GameObject*>(child)) {
                if (auto* found = search(go)) return found;
            }
        }
        return nullptr;
    };

    return search(root);
}

GameObject* findObjectByTag(SceneRegistry* registry, const std::string& tag) {
    GameObject* root = registry->getRoot();
    if (!root) return nullptr;

    std::function<GameObject*(GameObject*)> search = [&](GameObject* obj) -> GameObject* {
        if (obj->getTag() == tag) return obj;
        for (Entity* child : obj->getChildEntities()) {
            if (auto* go = dynamic_cast<GameObject*>(child)) {
                if (auto* found = search(go)) return found;
            }
        }
        return nullptr;
    };

    return search(root);
}

}  // namespace funshi
