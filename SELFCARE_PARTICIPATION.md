# Self-care participation directory
For those who would like to register to participate in the Self-Care portal Beta, you 
will be required to provide the following:
- A URI to the program within your organization
- Organization name
- URI to your Subscriber Self-care portal, including the port
- A logo of your company to be used in the rebranding of the application. This should be an SVG file of the 30px height, width 30px to 100px
- A logo of your company to be used in the directory itself. The format should be SVG and the height of 75px and wifth of 75px to 250 px.
- A URI to an access policy for your site
- A password policy RegEx and textual description.

## The Directory
The directory will be maintained by TIP and the above assets must be handed in for acceptance in the program.

## Location
The URI of the directory is located here:

https://s3.us-west-2.amazonaws.com/ucentral.arilia.com/tip_selfcare_registry.json

### The file format
The file actually contains a JSOn document containing an array of all the entries in the program. Here
is a sample

```json
{
  [
    {
      "org_uri" : "https://www.example.com" ,
      "org_name" : "Example Corp Inc.",
      "subscriber_portal" : "https://ucentral.dpaas.arilia.com:16006",
      "small_org_logo" : "https://www.example.com/logos/small_logo.svg",
      "large_org_logo" : "https://www.example.com/logos/big_logo.svg",
      "org_color_1" : "#3399ff",
      "access_policy" : "https://www.example.com/policies/selfcare_program_policy.html",
      "password_policy_regex" : "^(?=.*?[A-Z])(?=.*?[a-z])(?=.*?[0-9])(?=.*?[#?!@$%^&*-]).{8,}$",
      "password_policy" : "https://www.example.com/policies/selfcare_password_policy.html"
    },
    ...    
  ]
}
```

### The SelfCare application
The application is available on AppCenter by following this link.






