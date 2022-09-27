---
name: Feature Request
about: Suggest an idea for the hcnet-core implementation of the Hcnet Protocol
title: "[Short Description] (Version: [hcnet-core version])"
labels: enhancement
assignees: ''

---

## Read before creating an issue

In general, we close any issues that are
* unactionable (fill the template below under "Description")
* questions best served elsewhere

We have a small community of people that manages issues, and we want to ensure that the issues that remain open are high quality (so we actually get around to implementing them!).

### I have a question!

The hcnet-core issues repository is meant for reporting bugs and feature requests related to hcnet-core's implementation.

If you have a question, we would recommend that you take a look at Hcnet's [developer portal][1], where you'll find comprehensive documentation related to Hcnet.

If you can't find an answer to your question you can:
* submit a question to [Hcnet's Stack Exchange][2].
* or ask one of [Hcnet's communities][3].

[1]: https://www.hcnet.org/developers/
[2]: https://hcnet.stackexchange.com/
[3]: https://www.hcnet.org/community/#communities

### I'd like to request new functionality in hcnet-core!

First, you have to ask whether what you're trying to file is an issue related to Hcnet's Protocol
OR if it's related to `hcnet-core`, the C++ implementation that's in this repository.

Typically a request that changes how the core protocol works (such as adding a new operation, changing the way transactions work, etc) is best filed in the [Hcnet Protocol repository][4].

However, if your change is related to the implementation (say you'd like to see a new command line
flag or HTTP command added to hcnet-core), this is the place.

* Please check existing and closed issues in Github! You may have to remove the `is:open` filter.
* Check the [releases](https://github.com/hcnet/hcnet-core/releases) page to see if your request has already been added in a later release.

[4]: https://github.com/hcnet/hcnet-protocol/issues

## Description
### Explain in detail the additional functionality you would like to see in hcnet-core.

*Be descriptive, including the interface you'd like to see, as well as any suggestions you may have
with regard to its implementation.*

### Explain why this feature is important
*If it's related to some problem you're having, give a clear and concise description of what the problem is. Ex. I'm always frustrated when [...]*

### Describe the solution you'd like
*A clear and concise description of what you want to happen.*

### Describe alternatives you've considered
*A clear and concise description of any alternative solutions or features you've considered.*

### Additional context
*Add any other context about the feature request here, including any gists or attachments that would make it easier to understand the enhancement you're requesting.*
