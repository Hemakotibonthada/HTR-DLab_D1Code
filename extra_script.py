Import("env")

def before_upload(source, target, env):
    print("Uploading filesystem (SPIFFS/LittleFS) before firmware...")
    env.Execute("pio run --target uploadfs")

def after_upload(source, target, env):
    print("Firmware upload complete!")

env.AddPreAction("upload", before_upload)
env.AddPostAction("upload", after_upload)