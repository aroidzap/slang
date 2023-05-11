// slang-ast-builder.cpp
#include "slang-ast-builder.h"
#include <assert.h>

#include "slang-compiler.h"

namespace Slang {

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! SharedASTBuilder !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

SharedASTBuilder::SharedASTBuilder()
{    
}

void SharedASTBuilder::init(Session* session)
{
    m_namePool = session->getNamePool();

    // Save the associated session
    m_session = session;

    // We just want as a place to store allocations of shared types
    {
        RefPtr<ASTBuilder> astBuilder(new ASTBuilder);
        astBuilder->m_sharedASTBuilder = this;
        m_astBuilder = astBuilder.detach();
    }

    // Clear the built in types
    memset(m_builtinTypes, 0, sizeof(m_builtinTypes));

    // Create common shared types
    m_errorType = m_astBuilder->create<ErrorType>();
    m_bottomType = m_astBuilder->create<BottomType>();
    m_initializerListType = m_astBuilder->create<InitializerListType>();
    m_overloadedType = m_astBuilder->create<OverloadGroupType>();

    // We can just iterate over the class pointers.
    // NOTE! That this adds the names of the abstract classes too(!)
    for (Index i = 0; i < Index(ASTNodeType::CountOf); ++i)
    {
        const ReflectClassInfo* info = ASTClassInfo::getInfo(ASTNodeType(i));
        if (info)
        {
            m_sliceToTypeMap.add(UnownedStringSlice(info->m_name), info);
            Name* name = m_namePool->getName(String(info->m_name));
            m_nameToTypeMap.add(name, info);
        }
    }
}

const ReflectClassInfo* SharedASTBuilder::findClassInfo(const UnownedStringSlice& slice)
{
    const ReflectClassInfo* typeInfo;
    return m_sliceToTypeMap.tryGetValue(slice, typeInfo) ? typeInfo : nullptr;
}

SyntaxClass<NodeBase> SharedASTBuilder::findSyntaxClass(const UnownedStringSlice& slice)
{
    const ReflectClassInfo* typeInfo;
    if (m_sliceToTypeMap.tryGetValue(slice, typeInfo))
    {
        return SyntaxClass<NodeBase>(typeInfo);
    }
    return SyntaxClass<NodeBase>();
}

const ReflectClassInfo* SharedASTBuilder::findClassInfo(Name* name)
{
    const ReflectClassInfo* typeInfo;
    return m_nameToTypeMap.tryGetValue(name, typeInfo) ? typeInfo : nullptr;
}

SyntaxClass<NodeBase> SharedASTBuilder::findSyntaxClass(Name* name)
{
    const ReflectClassInfo* typeInfo;
    if (m_nameToTypeMap.tryGetValue(name, typeInfo))
    {
        return SyntaxClass<NodeBase>(typeInfo);
    }
    return SyntaxClass<NodeBase>();
}

Type* SharedASTBuilder::getStringType()
{
    if (!m_stringType)
    {
        auto stringTypeDecl = findMagicDecl("StringType");
        m_stringType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(stringTypeDecl));
    }
    return m_stringType;
}

Type* SharedASTBuilder::getNativeStringType()
{
    if (!m_nativeStringType)
    {
        auto nativeStringTypeDecl = findMagicDecl("NativeStringType");
        m_nativeStringType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(nativeStringTypeDecl));
    }
    return m_nativeStringType;
}

Type* SharedASTBuilder::getEnumTypeType()
{
    if (!m_enumTypeType)
    {
        auto enumTypeTypeDecl = findMagicDecl("EnumTypeType");
        m_enumTypeType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(enumTypeTypeDecl));
    }
    return m_enumTypeType;
}

Type* SharedASTBuilder::getDynamicType()
{
    if (!m_dynamicType)
    {
        auto dynamicTypeDecl = findMagicDecl("DynamicType");
        m_dynamicType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(dynamicTypeDecl));
    }
    return m_dynamicType;
}

Type* SharedASTBuilder::getNullPtrType()
{
    if (!m_nullPtrType)
    {
        auto nullPtrTypeDecl = findMagicDecl("NullPtrType");
        m_nullPtrType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(nullPtrTypeDecl));
    }
    return m_nullPtrType;
}

Type* SharedASTBuilder::getNoneType()
{
    if (!m_noneType)
    {
        auto noneTypeDecl = findMagicDecl("NoneType");
        m_noneType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(noneTypeDecl));
    }
    return m_noneType;
}

Type* SharedASTBuilder::getDiffInterfaceType()
{
    if (!m_diffInterfaceType)
    {
        auto decl = findMagicDecl("DifferentiableType");
        m_diffInterfaceType = DeclRefType::create(m_astBuilder, makeDeclRef<Decl>(decl));
    }
    return m_diffInterfaceType;
}

SharedASTBuilder::~SharedASTBuilder()
{
    // Release built in types..
    for (Index i = 0; i < SLANG_COUNT_OF(m_builtinTypes); ++i)
    {
        m_builtinTypes[i] = nullptr;
    }

    if (m_astBuilder)
    {
        m_astBuilder->releaseReference();
    }
}

void SharedASTBuilder::registerBuiltinDecl(Decl* decl, BuiltinTypeModifier* modifier)
{
    auto type = DeclRefType::create(m_astBuilder, DeclRef<Decl>(decl, nullptr));
    m_builtinTypes[Index(modifier->tag)] = type;
}

void SharedASTBuilder::registerBuiltinRequirementDecl(Decl* decl, BuiltinRequirementModifier* modifier)
{
    m_builtinRequirementDecls[modifier->kind] = decl;
}

void SharedASTBuilder::registerMagicDecl(Decl* decl, MagicTypeModifier* modifier)
{
    // In some cases the modifier will have been applied to the
    // "inner" declaration of a `GenericDecl`, but what we
    // actually want to register is the generic itself.
    //
    auto declToRegister = decl;
    if (auto genericDecl = as<GenericDecl>(decl->parentDecl))
        declToRegister = genericDecl;

    m_magicDecls[modifier->magicName] = declToRegister;
}

Decl* SharedASTBuilder::findMagicDecl(const String& name)
{
    return m_magicDecls[name].getValue();
}

Decl* SharedASTBuilder::tryFindMagicDecl(const String& name)
{
    if (m_magicDecls.containsKey(name))
    {
        return m_magicDecls[name].getValue();
    }
    else
    {
        return nullptr;
    }
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ASTBuilder !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

ASTBuilder::ASTBuilder(SharedASTBuilder* sharedASTBuilder, const String& name):
    m_sharedASTBuilder(sharedASTBuilder),
    m_name(name),
    m_id(sharedASTBuilder->m_id++),
    m_arena(2048)
{
    SLANG_ASSERT(sharedASTBuilder);
}

ASTBuilder::ASTBuilder():
    m_sharedASTBuilder(nullptr),
    m_id(-1),
    m_arena(2048)
{
    m_name = "SharedASTBuilder::m_astBuilder";
}

ASTBuilder::~ASTBuilder()
{
    for (NodeBase* node : m_dtorNodes)
    {
        const ReflectClassInfo* info = ASTClassInfo::getInfo(node->astNodeType);
        SLANG_ASSERT(info->m_destructorFunc);
        info->m_destructorFunc(node);
    }
}

NodeBase* ASTBuilder::createByNodeType(ASTNodeType nodeType)
{
    const ReflectClassInfo* info = ASTClassInfo::getInfo(nodeType);
    
    auto createFunc = info->m_createFunc;
    SLANG_ASSERT(createFunc);
    if (!createFunc)
    {
        return nullptr;
    }

    return (NodeBase*)createFunc(this);
}

Type* ASTBuilder::getSpecializedBuiltinType(Type* typeParam, char const* magicTypeName)
{
    auto declRef = getBuiltinDeclRef(magicTypeName, typeParam);
    auto rsType = DeclRefType::create(this, declRef);
    return rsType;
}

PtrType* ASTBuilder::getPtrType(Type* valueType)
{
    return dynamicCast<PtrType>(getPtrType(valueType, "PtrType"));
}

// Construct the type `Out<valueType>`
OutType* ASTBuilder::getOutType(Type* valueType)
{
    return dynamicCast<OutType>(getPtrType(valueType, "OutType"));
}

InOutType* ASTBuilder::getInOutType(Type* valueType)
{
    return dynamicCast<InOutType>(getPtrType(valueType, "InOutType"));
}

RefType* ASTBuilder::getRefType(Type* valueType)
{
    return dynamicCast<RefType>(getPtrType(valueType, "RefType"));
}

OptionalType* ASTBuilder::getOptionalType(Type* valueType)
{
    auto rsType = getSpecializedBuiltinType(valueType, "OptionalType");
    return as<OptionalType>(rsType);
}

PtrTypeBase* ASTBuilder::getPtrType(Type* valueType, char const* ptrTypeName)
{
    return as<PtrTypeBase>(getSpecializedBuiltinType(valueType, ptrTypeName));
}

ArrayExpressionType* ASTBuilder::getArrayType(Type* elementType, IntVal* elementCount)
{
    if (!elementCount)
        elementCount = getIntVal(getIntType(), kUnsizedArrayMagicLength);

    auto result = getOrCreate<ArrayExpressionType>(elementType, elementCount);
    if (!result->declRef.decl)
    {
        auto arrayGenericDecl = as<GenericDecl>(m_sharedASTBuilder->findMagicDecl("ArrayType"));
        auto arrayTypeDecl = arrayGenericDecl->inner;
        auto substitutions = getOrCreate<GenericSubstitution>(arrayGenericDecl, elementType, elementCount);
        result->declRef = DeclRef<Decl>(arrayTypeDecl, substitutions);
    }
    return result;
}

VectorExpressionType* ASTBuilder::getVectorType(
    Type*    elementType,
    IntVal*  elementCount)
{
    auto result = getOrCreate<VectorExpressionType>(elementType, elementCount);
    if (!result->declRef.decl)
    {
        auto vectorGenericDecl = as<GenericDecl>(m_sharedASTBuilder->findMagicDecl("Vector"));
        auto vectorTypeDecl = vectorGenericDecl->inner;
        auto substitutions = getOrCreate<GenericSubstitution>(vectorGenericDecl, elementType, elementCount);
        result->declRef = DeclRef<Decl>(vectorTypeDecl, substitutions);
    }
    return result;
}

DifferentialPairType* ASTBuilder::getDifferentialPairType(
    Type* valueType,
    Witness* primalIsDifferentialWitness)
{
    auto genericDecl = dynamicCast<GenericDecl>(m_sharedASTBuilder->findMagicDecl("DifferentialPairType"));

    auto typeDecl = genericDecl->inner;

    auto substitutions = getOrCreate<GenericSubstitution>(
        genericDecl,
        valueType,
        primalIsDifferentialWitness);

    auto declRef = DeclRef<Decl>(typeDecl, substitutions);
    auto rsType = DeclRefType::create(this, declRef);

    return as<DifferentialPairType>(rsType);
}

DeclRef<InterfaceDecl> ASTBuilder::getDifferentiableInterface()
{
    DeclRef<InterfaceDecl> declRef;
    declRef.decl = dynamicCast<InterfaceDecl>(m_sharedASTBuilder->findMagicDecl("DifferentiableType"));
    return declRef;
}

bool ASTBuilder::isDifferentiableInterfaceAvailable()
{
    return (m_sharedASTBuilder->tryFindMagicDecl("DifferentiableType") != nullptr);
}

MeshOutputType* ASTBuilder::getMeshOutputTypeFromModifier(
    HLSLMeshShaderOutputModifier* modifier,
    Type* elementType,
    IntVal* maxElementCount)
{
    SLANG_ASSERT(modifier);
    SLANG_ASSERT(elementType);
    SLANG_ASSERT(maxElementCount);

    const char* declName
        = as<HLSLVerticesModifier>(modifier) ? "VerticesType"
        : as<HLSLIndicesModifier>(modifier) ? "IndicesType"
        : as<HLSLPrimitivesModifier>(modifier) ? "PrimitivesType"
        : (SLANG_UNEXPECTED("Unhandled mesh output modifier"), nullptr);
    auto genericDecl = dynamicCast<GenericDecl>(m_sharedASTBuilder->findMagicDecl(declName));

    auto typeDecl = genericDecl->inner;

    auto substitutions = getOrCreate<GenericSubstitution>(
        genericDecl,
        elementType,
        maxElementCount);

    auto declRef = DeclRef<Decl>(typeDecl, substitutions);
    auto rsType = DeclRefType::create(this, declRef);

    return as<MeshOutputType>(rsType);
}

DeclRef<Decl> ASTBuilder::getBuiltinDeclRef(const char* builtinMagicTypeName, Val* genericArg)
{
    DeclRef<Decl> declRef;
    declRef.decl = m_sharedASTBuilder->findMagicDecl(builtinMagicTypeName);
    if (auto genericDecl = as<GenericDecl>(declRef.decl))
    {
        if (genericArg)
        {
            auto substitutions = getOrCreate<GenericSubstitution>(genericDecl, genericArg);
            declRef.substitutions = substitutions;
        }
        declRef.decl = genericDecl->inner;
    }
    else
    {
        SLANG_ASSERT(!genericArg);
    }
    return declRef;
}

Type* ASTBuilder::getAndType(Type* left, Type* right)
{
    auto type = getOrCreate<AndType>(left, right);
    return type;
}

Type* ASTBuilder::getModifiedType(Type* base, Count modifierCount, Val* const* modifiers)
{
    auto type = create<ModifiedType>();
    type->base = base;
    type->modifiers.addRange(modifiers, modifierCount);
    return type;
}

Val* ASTBuilder::getUNormModifierVal()
{
    return getOrCreate<UNormModifierVal>();
}

Val* ASTBuilder::getSNormModifierVal()
{
    return getOrCreate<SNormModifierVal>();
}

Val* ASTBuilder::getNoDiffModifierVal()
{
    return getOrCreate<NoDiffModifierVal>();
}

Type* ASTBuilder::getFuncType(List<Type*> parameters, Type* result)
{
    auto errorType = getOrCreate<BottomType>();
    return getOrCreate<FuncType>(parameters, result, errorType);
}

Type* ASTBuilder::getTupleType(List<Type*>& types)
{
    return getOrCreate<TupleType>(types);
}

TypeType* ASTBuilder::getTypeType(Type* type)
{
    return getOrCreate<TypeType>(type);
}

bool ASTBuilder::NodeDesc::operator==(NodeDesc const& that) const
{
    if(type != that.type) return false;
    if(operands.getCount() != that.operands.getCount()) return false;
    for(Index i = 0; i < operands.getCount(); ++i)
    {
        // Note: we are comparing the operands directly for identity
        // (pointer equality) rather than doing the `Val`-level
        // equality check.
        //
        // The rationale here is that nodes that will be created
        // via a `NodeDesc` *should* all be going through the
        // deduplication path anyway, as should their operands.
        // 
        if(operands[i].values.nodeOperand != that.operands[i].values.nodeOperand) return false;
    }
    return true;
}
HashCode ASTBuilder::NodeDesc::getHashCode() const
{
    Hasher hasher;
    hasher.hashValue(Int(type));
    for(Index i = 0; i < operands.getCount(); ++i)
    {
        // Note: we are hashing the raw pointer value rather
        // than the content of the value node. This is done
        // to match the semantics implemented for `==` on
        // `NodeDesc`.
        //
        hasher.hashValue(operands[i].values.nodeOperand);
    }
    return hasher.getResult();
}

} // namespace Slang
