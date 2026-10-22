package com.abdownloadmanager.service

import com.abdownloadmanager.shared.storage.IExtraDownloadItemSettings
import com.abdownloadmanager.shared.storage.IExtraQueueSettings
import kotlinx.serialization.KSerializer
import kotlinx.serialization.descriptors.SerialDescriptor
import kotlinx.serialization.encoding.Decoder
import kotlinx.serialization.encoding.Encoder
import kotlinx.serialization.json.*

/** Lossless adapter for the original desktop per-ID extra-settings JSON.
 * Stores are real, not placeholders. Platform action fields survive round trips,
 * but the explicitly disabled completion providers NEVER execute those actions.
 * Unknown fields are retained; this is not permission to import an existing profile.
 */
class StoredExtraSettings(val document: JsonObject) : IExtraDownloadItemSettings, IExtraQueueSettings {
    override val id: Long = document["id"]?.jsonPrimitive?.let {
        require(!it.isString); it.long
    } ?: error("Missing extra-settings ID")
    init {
        require(id >= 0)
        document["powerActionTypeOnFinish"]?.let {
            require(it == JsonNull || (it is JsonPrimitive && it.isString && it.content.length <= 128))
        }
        document["powerActionUseForceOnFinish"]?.let {
            require(it is JsonPrimitive && !it.isString && it.booleanOrNull != null)
        }
    }
    companion object : IExtraDownloadItemSettings.DataClassDefinitions<StoredExtraSettings>,
        IExtraQueueSettings.DataClassDefinitions<StoredExtraSettings> {
        override fun createDefault(id: Long) = StoredExtraSettings(buildJsonObject {
            put("id", id); put("powerActionTypeOnFinish", JsonNull); put("powerActionUseForceOnFinish", false)
        })
        override val serializer: KSerializer<StoredExtraSettings> = object : KSerializer<StoredExtraSettings> {
            override val descriptor: SerialDescriptor = JsonObject.serializer().descriptor
            override fun serialize(encoder: Encoder, value: StoredExtraSettings) =
                encoder.encodeSerializableValue(JsonObject.serializer(), value.document)
            override fun deserialize(decoder: Decoder) = StoredExtraSettings(decoder.decodeSerializableValue(JsonObject.serializer()))
        }
    }
}
