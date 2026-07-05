function makeFile(data, key) {
    if ((key + "Url") in data) {
        return {
            url: data[key + "Url"],
            width: data.width,
            height: data.height,
            filesize: data[key + "Size"],
        };
    }
    /*if (data.content_urls && key in data.content_urls) {
        return {
            url: data.content_urls[key].url,
            width: data.content_urls[key].width,
            height: data.content_urls[key].height,
            filesize: data.content_urls[key].size,
        };
    }*/
    return null;
}
export var downloader = {
    name: "Gfycat / Redgifs",
    handlers: [{
            regexes: ["(gfycat|redgifs|gifdeliverynetwork)\\.com/(?:watch/)?(\\w+)"],
            url: function (url) {
                var match = url.match(/(gfycat|redgifs|gifdeliverynetwork)\.com\/(?:watch\/)?(\w+).*$/i);
                if (!match) {
                    return { error: "Could not parse URL" };
                }
                var domain = match[1] === "gfycat" ? "gfycat" : "redgifs";
                var id = match[2];
                return "https://api.".concat(domain, ".com/v1/gfycats/").concat(id);
            },
            parse: function (src) {
                var data = JSON.parse(src).gfyItem;
                return {
                    tokens: Grabber.mapFields(data, {
                        "name": "title",
                        "author": "username",
                        "md5": "md5",
                        "source": "subreddit",
                        "created_at": "createDate",
                        "tags": "tags",
                    }),
                    files: [
                        makeFile(data, "mp4"),
                        /*makeFile(data, "gif"),
                        makeFile(data, "webm"),
                        makeFile(data, "max1mbGif"),
                        makeFile(data, "max2mbGif"),
                        makeFile(data, "max5mbGif"),*/
                        { url: data.posterUrl },
                    ].filter(function (f) { return f !== null; }),
                };
            },
        }],
};
