// Python/JSON contract extracted from config.cpp/runtime.cpp (licensed core origin).
#include <forge/engine.hpp>
#include <unordered_set>
#include <cmath>
namespace forge {
namespace {
struct SerializationFallback {};
Json convert(py::handle value,std::unordered_set<PyObject*>& ancestors,unsigned depth) {
    if(depth>256)throw SerializationFallback{};
    auto* object=value.ptr();
    if(object==Py_None)return nullptr;
    if(PyBool_Check(object))return object==Py_True;
    if(PyLong_CheckExact(object)) {
        int overflow=0;auto number=PyLong_AsLongLongAndOverflow(object,&overflow);
        if(overflow){PyErr_Clear();throw SerializationFallback{};}
        if(PyErr_Occurred())throw py::error_already_set();
        return number;
    }
    if(PyFloat_CheckExact(object)) {
        double number=PyFloat_AS_DOUBLE(object);
        if(!std::isfinite(number))throw SerializationFallback{};
        return number;
    }
    if(PyUnicode_CheckExact(object))return py::cast<std::string>(value);
    if(!PyDict_CheckExact(object) && !PyList_CheckExact(object) && !PyTuple_CheckExact(object))throw SerializationFallback{};
    if(!ancestors.insert(object).second)throw SerializationFallback{};
    // Ancestors are discarded with this conversion on any exception.
    Json result;
    if(PyDict_CheckExact(object)) {
        result=Json::object();PyObject *key,*item;Py_ssize_t position=0;
        while(PyDict_Next(object,&position,&key,&item)) {
            if(!PyUnicode_CheckExact(key))throw SerializationFallback{};
            result[py::cast<std::string>(py::handle(key))]=convert(py::handle(item),ancestors,depth+1);
        }
    } else {
        result=Json::array();auto count=PySequence_Size(object);result.get_ref<Json::array_t&>().reserve(size_t(count));
        for(Py_ssize_t i=0;i<count;++i)result.push_back(convert(py::handle(PyList_CheckExact(object)?PyList_GET_ITEM(object,i):PyTuple_GET_ITEM(object,i)),ancestors,depth+1));
    }
    ancestors.erase(object);return result;
}
}
Json fromPython(py::handle value) {
    try{std::unordered_set<PyObject*> ancestors;return convert(value,ancestors,0);}
    catch(const SerializationFallback&){
        // Retain JSON's non-string key coercion, subclasses, large integers and errors.
        return Json::parse(py::module_::import("json").attr("dumps")(value).cast<std::string>());
    }
}
static py::object convertPython(const Json& value,unsigned depth) {
    if(depth>256)throw SerializationFallback{};
    if(value.is_null())return py::none();
    if(value.is_boolean())return py::bool_(value.get<bool>());
    if(value.is_number_unsigned())return py::int_(value.get<uint64_t>());
    if(value.is_number_integer())return py::int_(value.get<int64_t>());
    if(value.is_number_float()){auto number=value.get<double>();return std::isfinite(number)?py::object(py::float_(number)):py::object(py::none());}
    if(value.is_string())return py::str(value.get_ref<const std::string&>());
    if(value.is_array()){py::list result;for(auto& item:value)result.append(convertPython(item,depth+1));return result;}
    if(value.is_object()){py::dict result;for(auto it=value.begin();it!=value.end();++it)result[py::str(it.key())]=convertPython(it.value(),depth+1);return result;}
    throw SerializationFallback{};
}
py::object pythonValue(const Json& value) {
    try{return convertPython(value,0);}
    catch(const SerializationFallback&){return py::module_::import("json").attr("loads")(value.dump());}
}
} // namespace forge
