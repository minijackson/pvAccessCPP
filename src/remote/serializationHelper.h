/*
 * serializationHelper.h
 *
 *  Created on: Jul 24, 2012
 *      Author: msekoranja
 */

#ifndef SERIALIZATIONHELPER_H_
#define SERIALIZATIONHELPER_H_

#ifdef epicsExportSharedSymbols
#   define serializationHelperEpicsExportSharedSymbols
#   undef epicsExportSharedSymbols
#endif

#include <pv/serialize.h>
#include <pv/pvData.h>
#include <pv/convert.h>
#include <pv/noDefaultMethods.h>
#include <pv/pvIntrospect.h>
#include <pv/byteBuffer.h>

#ifdef serializationHelperEpicsExportSharedSymbols
#   define epicsExportSharedSymbols
#	undef serializationHelperEpicsExportSharedSymbols
#endif

#include <pv/pvaConstants.h>
#include <pv/pvAccess.h>

namespace epics {
    namespace pvAccess {

        class epicsShareClass SerializationHelper : public epics::pvData::NoDefaultMethods {
        public:

    		static epics::pvData::PVDataCreatePtr _pvDataCreate;
            static epics::pvData::ConvertPtr _convert;

			/**
			 * Deserialize PVRequest.
			 * @param payloadBuffer data buffer.
			 * @return deserialized PVRequest, can be <code>null</code>.
			 */
			static epics::pvData::PVStructure::shared_pointer deserializePVRequest(epics::pvData::ByteBuffer* payloadBuffer, epics::pvData::DeserializableControl* control);

			/**
			 * Deserialize Structure and create PVStructure instance.
			 * @param payloadBuffer data buffer.
			 * @param control deserialization control.
			 * @return PVStructure instance, can be <code>null</code>.
			 */
			static epics::pvData::PVStructure::shared_pointer deserializeStructureAndCreatePVStructure(epics::pvData::ByteBuffer* payloadBuffer, epics::pvData::DeserializableControl* control);

			/**
			 * Deserialize Structure and create PVStructure instance, if necessary.
			 * @param payloadBuffer data buffer.
			 * @param control deserialization control.
			 * @param existingStructure if deserialized Field matches <code>existingStrcuture</code> Field, then
			 * 			<code>existingStructure</code> instance is returned. <code>null</code> value is allowed.
			 * @return PVStructure instance, can be <code>null</code>.
			 */
			static epics::pvData::PVStructure::shared_pointer deserializeStructureAndCreatePVStructure(epics::pvData::ByteBuffer* payloadBuffer, epics::pvData::DeserializableControl* control, epics::pvData::PVStructure::shared_pointer const & existingStructure);

			/**
			 * Deserialize optional PVStructrue.
			 * @param payloadBuffer data buffer.
			 * @return deserialized PVStructure, can be <code>null</code>.
			 */
			static epics::pvData::PVStructure::shared_pointer deserializeStructureFull(epics::pvData::ByteBuffer* payloadBuffer, epics::pvData::DeserializableControl* control);

            /**
             * Deserialize optional PVField.
             * @param payloadBuffer data buffer.
             * @return deserialized PVField, can be <code>null</code>.
             */
            static epics::pvData::PVField::shared_pointer deserializeFull(epics::pvData::ByteBuffer* payloadBuffer, epics::pvData::DeserializableControl* control);

            /**
             * Serialize <code>null</code> PVField.
             * @param buffer
             * @param control
             */
            static void serializeNullField(epics::pvData::ByteBuffer* buffer, epics::pvData::SerializableControl* control);

			/**
			 * Serialize PVRequest.
			 * @param buffer data buffer.
			 */
			static void serializePVRequest(epics::pvData::ByteBuffer* buffer, epics::pvData::SerializableControl* control, epics::pvData::PVStructure::shared_pointer const & pvRequest);

			/**
			 * Serialize optional PVStructrue.
			 * @param buffer data buffer.
			 */
			static void serializeStructureFull(epics::pvData::ByteBuffer* buffer, epics::pvData::SerializableControl* control, epics::pvData::PVStructure::shared_pointer const & pvStructure);

            /**
             * Serialize optional PVField.
             * @param buffer data buffer.
             */
            static void serializeFull(epics::pvData::ByteBuffer* buffer, epics::pvData::SerializableControl* control, epics::pvData::PVField::shared_pointer const & pvField);

            static void copyUnchecked(
                             epics::pvData::PVField::shared_pointer const & from,
                             epics::pvData::PVField::shared_pointer const & to);

            static void copyStructureUnchecked(
                             epics::pvData::PVStructure::shared_pointer const & from,
                             epics::pvData::PVStructure::shared_pointer const & to);

            // TODO move somewhere else, to pvData?
            static void partialCopy(
                             epics::pvData::PVStructure::shared_pointer const & from,
                             epics::pvData::PVStructure::shared_pointer const & to,
                             epics::pvData::BitSet::shared_pointer const & maskBitSet,
                             bool inverse = false);

};

}
}

#endif /* SERIALIZATIONHELPER_H_ */
